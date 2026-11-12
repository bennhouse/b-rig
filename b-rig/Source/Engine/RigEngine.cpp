#include "RigEngine.h"

namespace rig
{
namespace
{
    juce::CriticalSection& chainLock()   // modifiche della catena (messaggi) contro prepare() (thread del dispositivo)
    {
        static juce::CriticalSection cs;
        return cs;
    }
    constexpr int kMaxChunk = 4096;
}

RigEngine::RigEngine()
{
    midi.ensureSize (1024);
    live.store (new Chain());
}

RigEngine::~RigEngine()
{
    delete live.exchange (nullptr);
    retired.clear();
    order.clear();
}

// ---------------------------------------------------------------- preparazione

void RigEngine::prepare (double sampleRate, int maxBlockSize)
{
    const juce::ScopedLock sl (chainLock());
    fs = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jlimit (16, kMaxChunk, maxBlockSize > 0 ? maxBlockSize : 512);
    work.setSize (2, maxBlock, false, true, false);
    metroBuf.assign ((size_t) maxBlock, 0.0f);
    for (auto& s : order) prepareSlot (*s);

    // tuner: due passa-basso di Butterworth a 1 kHz prima della decimazione 4x (anche le note alte arrivano piene)
    const double w = juce::MathConstants<double>::twoPi * 1000.0 / fs, cw = std::cos (w), al = std::sin (w) / (2.0 * 0.70710678);
    const double a0 = 1.0 + al;
    for (auto* lp : { &tunerLp1, &tunerLp2 })
    {
        lp->b0 = lp->b2 = (float) ((1.0 - cw) * 0.5 / a0);
        lp->b1 = (float) ((1.0 - cw) / a0);
        lp->a1 = (float) (-2.0 * cw / a0);
        lp->a2 = (float) ((1.0 - al) / a0);
        lp->z1 = lp->z2 = 0;
    }
    limRelease = (float) (1.0 - std::exp (-1.0 / (0.08 * fs)));   // rilascio del limiter: 80 ms
    // code dei suoni del metronomo: beep 25 ms, hat 30 ms, wood 12 ms
    decays = { (float) std::exp (-1.0 / (0.025 * fs)), (float) std::exp (-1.0 / (0.030 * fs)), (float) std::exp (-1.0 / (0.012 * fs)) };
    muteStep = (float) (1.0 / (0.010 * fs));                      // mute: 10 ms
    limGain = 1.0f;
    prepared = true;
}

void RigEngine::release()
{
    const juce::ScopedLock sl (chainLock());
    prepared = false;
    for (auto& s : order) if (s->proc) s->proc->releaseResources();
}

void RigEngine::prepareSlot (Slot& s)
{
    s.dry.setSize (2, maxBlock, false, true, false);
    if (s.proc == nullptr) return;
    s.proc->setPlayHead (&playHead);
    s.proc->setNonRealtime (false);
    s.proc->setRateAndBufferSizeDetails (fs, maxBlock);
    s.proc->prepareToPlay (fs, maxBlock);
    s.inCh = juce::jlimit (1, 2, s.proc->getTotalNumInputChannels());
    s.outCh = juce::jlimit (1, 2, s.proc->getTotalNumOutputChannels());
}

// ---------------------------------------------------------------- catena (thread dei messaggi)

void RigEngine::publish()
{
    auto c = std::make_unique<Chain>();
    for (auto& s : order) if (c->n < kMaxSlots) c->s[(size_t) c->n++] = s.get();
    Chain* old = live.exchange (c.release());
    retired.push_back ({ std::unique_ptr<Chain> (old), {}, audioTicks.load() });
}

void RigEngine::collectGarbage()
{
    const auto t = audioTicks.load();
    for (auto it = retired.begin(); it != retired.end();)
        // il callback che poteva avere ancora la vecchia catena è finito quando ne è partito uno nuovo
        if (! prepared || t > it->tick) it = retired.erase (it); else ++it;
}

int RigEngine::indexOf (const Slot* s) const
{
    for (size_t i = 0; i < order.size(); ++i) if (order[i].get() == s) return (int) i;
    return -1;
}

bool RigEngine::cabAfter (int index, bool needIr) const
{
    for (size_t k = (size_t) juce::jmax (0, index + 1); k < order.size(); ++k)
    {
        const auto& s = *order[k];
        if (s.info->hasCab && s.proc && (! needIr || s.info->hasCab (*s.proc))) return true;
    }
    return false;
}

std::vector<const Slot*> RigEngine::coveredAmps() const
{
    std::vector<const Slot*> v;
    for (size_t i = 0; i < order.size(); ++i)
        if (order[i]->info->safeArm && order[i]->enabled.load() && cabAfter ((int) i, false)) v.push_back (order[i].get());
    return v;
}

void RigEngine::remuteUncovered (const std::vector<const Slot*>& before)
{
    for (size_t i = 0; i < order.size(); ++i)
        if (std::find (before.begin(), before.end(), order[i].get()) != before.end() && ! cabAfter ((int) i, false))
            order[i]->enabled = false;   // la cassa non c'è più dopo l'ampli: di nuovo muto, va riarmato
}

int RigEngine::logicalPosition (const ModuleInfo& m) const
{
    int pos = 0;
    for (size_t i = 0; i < order.size(); ++i) if (order[i]->info->order <= m.order) pos = (int) i + 1;
    return pos;
}

Slot* RigEngine::addModule (const juce::String& id, int position)
{
    const auto* m = findModule (id);
    if (m == nullptr || (int) order.size() >= kMaxSlots) return nullptr;
    auto s = std::make_unique<Slot>();
    s->info = m;
    if (m->available()) s->proc = m->make();
    // b-amp parte SEMPRE spento (protezione timpani); i segnaposto non suonano
    s->enabled = m->available() && ! m->safeArm;
    const juce::ScopedLock sl (chainLock());
    if (prepared) prepareSlot (*s);
    else if (s->proc) { s->inCh = juce::jlimit (1, 2, s->proc->getTotalNumInputChannels()); s->outCh = juce::jlimit (1, 2, s->proc->getTotalNumOutputChannels()); }
    if (position < 0) position = logicalPosition (*m);
    position = juce::jlimit (0, (int) order.size(), position);
    auto* raw = s.get();
    order.insert (order.begin() + position, std::move (s));
    publish();
    if (onChainChanged) onChainChanged();
    return raw;
}

void RigEngine::removeSlot (int index)
{
    if (index < 0 || index >= (int) order.size()) return;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    auto s = std::move (order[(size_t) index]);
    order.erase (order.begin() + index);
    remuteUncovered (covered);
    publish();
    retired.back().slots.push_back (std::move (s));   // si libera con la catena che lo conteneva
    if (onChainChanged) onChainChanged();
}

void RigEngine::moveSlot (int index, int delta)
{
    const int to = index + delta;
    if (index < 0 || index >= (int) order.size() || to < 0 || to >= (int) order.size()) return;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    std::swap (order[(size_t) index], order[(size_t) to]);
    remuteUncovered (covered);
    publish();
    if (onChainChanged) onChainChanged();
}

void RigEngine::moveSlotTo (int from, int to)
{
    const int n = (int) order.size();
    if (from < 0 || from >= n) return;
    to = juce::jlimit (0, n - 1, to);
    if (to == from) return;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    auto s = std::move (order[(size_t) from]);
    order.erase (order.begin() + from);
    order.insert (order.begin() + to, std::move (s));
    remuteUncovered (covered);
    publish();
    if (onChainChanged) onChainChanged();
}

void RigEngine::setEnabled (int index, bool on)
{
    if (index < 0 || index >= (int) order.size()) return;
    auto& s = *order[(size_t) index];
    s.enabled = on && s.proc != nullptr;
}

void RigEngine::clear()
{
    const juce::ScopedLock sl (chainLock());
    std::vector<std::unique_ptr<Slot>> gone;
    for (auto& s : order) gone.push_back (std::move (s));
    order.clear();
    publish();
    for (auto& s : gone) retired.back().slots.push_back (std::move (s));
}

void RigEngine::resetDefault()
{
    auto cb = std::move (onChainChanged);
    clear();
    addModule ("drop", 0);
    addModule ("amp", 1);
    addModule ("blend", 2);
    addModule ("aftercab", 3);
    setEnabled (0, false);
    setEnabled (1, false);   // b-amp: safe muted
    onChainChanged = std::move (cb);
    if (onChainChanged) onChainChanged();
}

juce::ValueTree RigEngine::saveRig() const
{
    juce::ValueTree t ("BRIG");
    t.setProperty ("version", 1, nullptr);
    for (auto& s : order)
    {
        juce::ValueTree c ("SLOT");
        c.setProperty ("id", s->info->id, nullptr);
        c.setProperty ("enabled", s->enabled.load(), nullptr);
        if (s->proc)
        {
            juce::MemoryBlock mb;
            s->proc->getStateInformation (mb);
            c.setProperty ("state", mb.toBase64Encoding(), nullptr);
        }
        t.appendChild (c, nullptr);
    }
    return t;
}

void RigEngine::loadRig (const juce::ValueTree& t)
{
    if (! t.hasType ("BRIG")) return;
    auto cb = std::move (onChainChanged);
    clear();
    std::vector<Slot*> armed;
    for (auto c : t)
    {
        auto* s = addModule (c["id"].toString(), numSlots());
        if (s == nullptr) continue;
        if (s->proc && c.hasProperty ("state"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (c["state"].toString())) s->proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        s->enabled = s->proc != nullptr && ! s->info->safeArm && (bool) c.getProperty ("enabled", true);
        if (s->info->safeArm && s->proc != nullptr && (bool) c.getProperty ("enabled", true)) armed.push_back (s);
    }
    // b-amp da un preset riparte acceso solo se dopo di lui c'è un b-blend con un IR caricato (la cassa);
    // altrimenti resta muto e si arma a mano
    for (auto* s : armed) if (cabAfter (indexOf (s), true)) s->enabled = true;
    onChainChanged = std::move (cb);
    if (onChainChanged) onChainChanged();
}

// ---------------------------------------------------------------- audio

void RigEngine::runProcessor (Slot& s, int n) noexcept
{
    float* ch[2] = { work.getWritePointer (0), work.getWritePointer (1) };
    if (s.inCh == 1)   // modulo mono: somma L/R (identica se il segnale è ancora mono)
        for (int i = 0; i < n; ++i) ch[0][i] = 0.5f * (ch[0][i] + ch[1][i]);
    juce::AudioBuffer<float> buf (ch, juce::jmax (s.inCh, s.outCh), n);
    midi.clear();
    s.proc->processBlock (buf, midi);
    if (s.outCh == 1) juce::FloatVectorOperations::copy (ch[1], ch[0], n);
    s.processedBlocks.fetch_add (1, std::memory_order_relaxed);
}

void RigEngine::processSlot (Slot& s, int n) noexcept
{
    if (s.proc == nullptr) return;
    const bool tgt = s.enabled.load (std::memory_order_relaxed);
    if (tgt != s.fadeTarget)
    {
        s.fadeTarget = tgt;
        // accensione di b-amp: fade-in morbido di 80 ms; tutto il resto: crossfade di 10 ms
        const double ms = (tgt && s.info->safeArm) ? 80.0 : 10.0;
        s.fadeStep = (float) (1.0 / (ms * 0.001 * fs));
    }
    if (s.fade <= 0.0f && ! tgt) return;          // spento: processBlock saltato, 0 % CPU
    if (s.fade >= 1.0f && tgt) { runProcessor (s, n); return; }

    // cambio di stato: crossfade cosinusoidale tra il segnale senza e con il modulo
    for (int c = 0; c < 2; ++c) s.dry.copyFrom (c, 0, work, c, 0, n);
    runProcessor (s, n);
    float* w0 = work.getWritePointer (0);
    float* w1 = work.getWritePointer (1);
    const float* d0 = s.dry.getReadPointer (0);
    const float* d1 = s.dry.getReadPointer (1);
    const float dir = tgt ? s.fadeStep : -s.fadeStep;
    float x = s.fade;
    for (int i = 0; i < n; ++i)
    {
        x = juce::jlimit (0.0f, 1.0f, x + dir);
        const float g = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
        w0[i] = d0[i] + (w0[i] - d0[i]) * g;
        w1[i] = d1[i] + (w1[i] - d1[i]) * g;
    }
    s.fade = x;
}

bool RigEngine::metronome (int n) noexcept
{
    if (! metroOn.load (std::memory_order_relaxed))
    {
        beat = -1; samplesToBeat = 0; clickEnv = 0; ppq = 0;
        beatInBar.store (-1, std::memory_order_relaxed);
        return false;
    }
    const auto sig = kSigs[(size_t) juce::jlimit (0, kNumSigs - 1, sigIndex.load (std::memory_order_relaxed))];
    float* m = metroBuf.data();
    std::fill (m, m + n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        if (samplesToBeat <= 0.0)
        {
            beat = (beat + 1) % sig.first;
            beatInBar.store (beat, std::memory_order_relaxed);
            clickSound = juce::jlimit (0, kNumSounds - 1, metroSound.load (std::memory_order_relaxed));
            const double f = clickSound == 2 ? (beat == 0 ? 1250.0 : 950.0) : (beat == 0 ? 2000.0 : 1300.0);
            clickInc = (float) (juce::MathConstants<double>::twoPi * f / fs);
            clickInc2 = clickInc * 2.76f;   // seconda risonanza del legno
            clickPhase = clickPhase2 = 0;
            clickDecay = decays[(size_t) clickSound];
            clickEnv = (beat == 0 ? 0.47f : 0.29f) * juce::jlimit (0.0f, 1.0f, metroVolume.load (std::memory_order_relaxed));
            const double quarter = 60.0 / juce::jlimit (kMinBpm, kMaxBpm, bpm.load (std::memory_order_relaxed)) * fs;
            samplesToBeat += sig.second == 8 ? quarter * 0.5 : quarter;
            ppq += sig.second == 8 ? 0.5 : 1.0;
        }
        samplesToBeat -= 1.0;
        if (clickEnv > 1.0e-4f)
        {
            float v;
            if (clickSound == 1)   // hat: rumore con passa-alto del 2° ordine (solo la parte metallica)
            {
                noise = noise * 1664525u + 1013904223u;
                const float nz = (float) (noise >> 8) * (2.0f / 16777216.0f) - 1.0f;
                v = clickEnv * (nz - 2.0f * clickPrev + clickPrev2) * 0.35f;
                clickPrev2 = clickPrev; clickPrev = nz;
            }
            else if (clickSound == 2) v = clickEnv * (std::sin (clickPhase) + 0.45f * std::sin (clickPhase2));
            else v = clickEnv * std::sin (clickPhase);
            m[i] = v;
            clickPhase += clickInc;
            clickPhase2 += clickInc2;
            clickEnv *= clickDecay;
        }
    }
    return true;
}

void RigEngine::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept
{
    audioTicks.fetch_add (1);
    Chain* chain = live.load();
    if (! prepared || chain == nullptr)
    {
        for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], numSamples);
        return;
    }
    const int di = diChannel.load (std::memory_order_relaxed);
    const float* src = (di >= 0 && di < numIn) ? in[di] : nullptr;
    float inPk = inPeak.load (std::memory_order_relaxed), outPk = outPeak.load (std::memory_order_relaxed);
    float minGain = 1.0f;

    for (int off = 0; off < numSamples; off += maxBlock)
    {
        const int n = juce::jmin (maxBlock, numSamples - off);
        float* w0 = work.getWritePointer (0);
        float* w1 = work.getWritePointer (1);
        if (src != nullptr) juce::FloatVectorOperations::copy (w0, src + off, n);
        else juce::FloatVectorOperations::clear (w0, n);
        const auto rng = juce::FloatVectorOperations::findMinAndMax (w0, n);
        const float pk = juce::jmax (-rng.getStart(), rng.getEnd());
        inPk = juce::jmax (inPk, pk);
        if (pk >= 0.999f) inClip.store (true, std::memory_order_relaxed);

        // tuner: DI pulita, passa-basso e decimata 4x (spento = nessun calcolo)
        if (tunerOn.load (std::memory_order_relaxed))
        {
            std::array<float, kMaxChunk / 4 + 1> dec;
            int nd = 0;
            for (int i = 0; i < n; ++i)
            {
                const float y = tunerLp2.process (tunerLp1.process (w0[i]));
                if (++tunerDecim >= 4) { tunerDecim = 0; dec[(size_t) nd++] = y; }
            }
            if (nd > 0 && tunerFifo.getFreeSpace() >= nd)
            {
                const auto scope = tunerFifo.write (nd);
                if (scope.blockSize1 > 0) std::copy (dec.begin(), dec.begin() + scope.blockSize1, tunerBuf.begin() + scope.startIndex1);
                if (scope.blockSize2 > 0) std::copy (dec.begin() + scope.blockSize1, dec.begin() + nd, tunerBuf.begin() + scope.startIndex2);
            }
        }
        juce::FloatVectorOperations::copy (w1, w0, n);

        // playhead: i bpm del metronomo per i moduli sincronizzati al tempo
        playHead.info.setBpm (juce::jlimit (kMinBpm, kMaxBpm, bpm.load (std::memory_order_relaxed)));
        const auto sig = kSigs[(size_t) juce::jlimit (0, kNumSigs - 1, sigIndex.load (std::memory_order_relaxed))];
        playHead.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { sig.first, sig.second });
        playHead.info.setIsPlaying (metroOn.load (std::memory_order_relaxed));
        playHead.info.setPpqPosition (ppq);

        for (int k = 0; k < chain->n; ++k) processSlot (*chain->s[(size_t) k], n);

        // mute del tuner (fade di 10 ms) sulla chitarra
        const float muteTarget = globalMute.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
        if (std::abs (muteGain - muteTarget) > 0.0f || muteTarget < 0.5f)
            for (int i = 0; i < n; ++i)
            {
                muteGain = muteTarget > muteGain ? juce::jmin (muteTarget, muteGain + muteStep) : juce::jmax (muteTarget, muteGain - muteStep);
                w0[i] *= muteGain; w1[i] *= muteGain;
            }

        // limiter di sicurezza finale a -0,5 dBFS: attacco istantaneo (nessuna latenza), rilascio 80 ms, poi clip netto
        for (int i = 0; i < n; ++i)
        {
            if (! std::isfinite (w0[i])) w0[i] = 0;   // un modulo impazzito non arriva alle casse
            if (! std::isfinite (w1[i])) w1[i] = 0;
            const float p = juce::jmax (std::abs (w0[i]), std::abs (w1[i]));
            limGain += (1.0f - limGain) * limRelease;
            if (p * limGain > kCeiling) { limGain = kCeiling / p; outClip.store (true, std::memory_order_relaxed); }
            minGain = juce::jmin (minGain, limGain);
            w0[i] = juce::jlimit (-kCeiling, kCeiling, w0[i] * limGain);
            w1[i] = juce::jlimit (-kCeiling, kCeiling, w1[i] * limGain);
        }
        // metronomo in parallelo: non passa dalla catena né dal limiter della chitarra (che non lo schiaccia), si
        // somma alla fine; un clip a 0 dBFS resta come ultima protezione
        if (metronome (n))
            for (int i = 0; i < n; ++i)
            {
                w0[i] = juce::jlimit (-0.999f, 0.999f, w0[i] + metroBuf[(size_t) i]);
                w1[i] = juce::jlimit (-0.999f, 0.999f, w1[i] + metroBuf[(size_t) i]);
            }
        const auto r0 = juce::FloatVectorOperations::findMinAndMax (w0, n);
        const auto r1 = juce::FloatVectorOperations::findMinAndMax (w1, n);
        outPk = juce::jmax (outPk, -r0.getStart(), r0.getEnd(), juce::jmax (-r1.getStart(), r1.getEnd()));

        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr)
            {
                if (c < 2) juce::FloatVectorOperations::copy (out[c] + off, c == 0 ? w0 : w1, n);
                else juce::FloatVectorOperations::clear (out[c] + off, n);
            }
    }
    inPeak.store (inPk, std::memory_order_relaxed);
    outPeak.store (outPk, std::memory_order_relaxed);
    limiterGr.store (minGain, std::memory_order_relaxed);
}
}
