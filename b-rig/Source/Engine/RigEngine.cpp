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
    workB.setSize (2, maxBlock, false, true, false);
    for (auto& l : laneLine) l.assign ((size_t) kWidenBuf, 0.0f);
    widenW = 0; delayCur[0] = delayCur[1] = 0.0f;
    widenSmooth = (float) (1.0 - std::exp (-1.0 / (0.05 * fs)));   // il ritardo cambia in circa 50 ms, senza click
    capA.assign ((size_t) (0.75 * fs), 0.0f);
    capB.assign ((size_t) (0.75 * fs) + (size_t) (0.026 * fs), 0.0f);
    capPos = 0;
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
    c->split = split;
    for (auto& s : order)   // l'elenco è sempre ordinato per ramo: prima dello split, A, B
        if (c->n < kMaxSlots)
        {
            c->s[(size_t) c->n++] = s.get();
            if (s->lane == 0) ++c->nTrunk; else if (s->lane == 1) ++c->nA;
        }
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

int RigEngine::laneSize (int lane) const
{
    int n = 0;
    for (auto& s : order) n += s->lane == lane ? 1 : 0;
    return n;
}

int RigEngine::laneStart (int lane) const
{
    int n = 0;
    for (auto& s : order) if (s->lane < lane) ++n;
    return n;
}

bool RigEngine::laneHas (int lane, const juce::String& id, const Slot* except) const
{
    for (auto& s : order) if (s->lane == lane && s.get() != except && s->info->id == id) return true;
    return false;
}

bool RigEngine::canPlace (const juce::String& id, int lane, const Slot* except) const
{
    if (! split || lane == 0) { for (auto& s : order) if (s.get() != except && s->info->id == id) return false; return true; }
    return ! laneHas (0, id, except) && ! laneHas (lane, id, except);
}

bool RigEngine::isCab (const Slot& s, bool needIr) const
{
    return s.info->hasCab && s.proc && (! needIr || s.info->hasCab (*s.proc));
}

bool RigEngine::cabAfter (int index, bool needIr) const
{
    if (! juce::isPositiveAndBelow (index, (int) order.size())) return false;
    const int lane = order[(size_t) index]->lane;
    for (size_t k = (size_t) index + 1; k < order.size(); ++k)
        if (order[k]->lane == lane && isCab (*order[k], needIr)) return true;
    if (lane != 0 || ! split) return false;
    // prima dello split: serve una cassa in entrambi i rami
    auto laneCab = [&] (int l) { for (auto& s : order) if (s->lane == l && isCab (*s, needIr)) return true; return false; };
    return laneCab (1) && laneCab (2);
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
        {
            order[i]->enabled = false;   // la cassa non c'è più dopo l'ampli: di nuovo muto, va riarmato
            order[i]->safeMuted = true;
        }
}

// b-amp muto per protezione con un b-blend dopo di sé: si arma (fade-in di 80 ms). Anche senza IR: b-blend senza IR
// blocca l'uscita da solo. Un ampli spento dall'utente resta spento.
void RigEngine::armCovered()
{
    for (size_t i = 0; i < order.size(); ++i)
    {
        auto& s = *order[i];
        if (s.info->safeArm && s.proc != nullptr && s.safeMuted.load() && ! s.enabled.load() && cabAfter ((int) i, false))
        { s.enabled = true; s.safeMuted = false; }
    }
}

int RigEngine::logicalPosition (const ModuleInfo& m) const
{
    int pos = 0, k = 0;
    for (auto& s : order) if (s->lane == 0) { ++k; if (s->info->order <= m.order) pos = k; }
    return pos;
}

std::unique_ptr<Slot> RigEngine::makeSlot (const ModuleInfo& m)
{
    auto s = std::make_unique<Slot>();
    s->info = &m;
    if (m.available()) s->proc = m.make();
    // b-amp parte SEMPRE muto (protezione timpani); i segnaposto non suonano
    s->enabled = m.available() && ! m.safeArm;
    s->safeMuted = m.available() && m.safeArm;
    if (s->proc) { s->inCh = juce::jlimit (1, 2, s->proc->getTotalNumInputChannels()); s->outCh = juce::jlimit (1, 2, s->proc->getTotalNumOutputChannels()); }
    return s;
}

std::unique_ptr<Slot> RigEngine::cloneSlot (const Slot& src)
{
    auto s = makeSlot (*src.info);
    if (s->proc && src.proc)
    {
        juce::MemoryBlock mb;
        src.proc->getStateInformation (mb);
        s->proc->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    s->enabled = s->proc != nullptr && src.enabled.load();
    s->safeMuted = src.safeMuted.load();
    s->lane = src.lane;
    return s;
}

// chiamata con chainLock preso
void RigEngine::insertSlot (std::unique_ptr<Slot> s, int lane, int pos)
{
    if (prepared) prepareSlot (*s);
    s->lane = split ? juce::jlimit (0, 2, lane) : 0;
    const int at = laneStart (s->lane) + juce::jlimit (0, laneSize (s->lane), pos);
    order.insert (order.begin() + at, std::move (s));
}

Slot* RigEngine::addModule (const juce::String& id, int position)
{
    return addModuleAt (id, 0, position);
}

Slot* RigEngine::addModuleAt (const juce::String& id, int lane, int pos)
{
    const auto* m = findModule (id);
    if (m == nullptr || (int) order.size() >= kMaxSlots) return nullptr;
    if (! split) lane = 0;
    auto s = makeSlot (*m);
    if (pos < 0)
    {
        // posizione logica nel ramo: dopo l'ultimo modulo che nella catena della suite viene prima
        pos = 0; int k = 0;
        for (auto& o : order) if (o->lane == lane) { ++k; if (o->info->order <= m->order) pos = k; }
    }
    const juce::ScopedLock sl (chainLock());
    auto* raw = s.get();
    insertSlot (std::move (s), lane, pos);
    armCovered();
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

bool RigEngine::placeSlot (int index, int lane, int pos)
{
    if (index < 0 || index >= (int) order.size()) return false;
    if (! split) lane = 0;
    auto& src = *order[(size_t) index];
    if (! canPlace (src.info->id, lane, &src)) return false;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    auto s = std::move (order[(size_t) index]);
    order.erase (order.begin() + index);
    const int at = laneStart (lane) + juce::jlimit (0, laneSize (lane), pos);
    s->lane = lane;
    order.insert (order.begin() + at, std::move (s));
    remuteUncovered (covered);
    armCovered();
    publish();
    if (onChainChanged) onChainChanged();
    return true;
}

Slot* RigEngine::copyToLane (int index, int lane)
{
    if (! split || index < 0 || index >= (int) order.size() || (int) order.size() >= kMaxSlots) return nullptr;
    const auto& src = *order[(size_t) index];
    if (src.lane == 0 || (lane != 1 && lane != 2) || lane == src.lane || ! canPlace (src.info->id, lane)) return nullptr;
    auto c = cloneSlot (src);
    // posizione logica nel ramo: dopo l'ultimo modulo che nella catena della suite viene prima
    int pos = 0, k = 0;
    for (auto& o : order) if (o->lane == lane) { ++k; if (o->info->order <= src.info->order) pos = k; }
    const juce::ScopedLock sl (chainLock());
    auto* raw = c.get();
    insertSlot (std::move (c), lane, pos);
    armCovered();
    publish();
    if (onChainChanged) onChainChanged();
    return raw;
}

int RigEngine::latencySamples() const
{
    int trunk = 0, laneLat[3] = { 0, 0, 0 };
    for (auto& s : order)
        if (s->proc != nullptr && s->enabled.load())
            (s->lane == 0 ? trunk : laneLat[s->lane]) += s->proc->getLatencySamples();
    return trunk + juce::jmax (laneLat[1], laneLat[2]);
}

void RigEngine::moveSlot (int index, int delta)
{
    if (index < 0 || index >= (int) order.size()) return;
    const int lane = order[(size_t) index]->lane, pos = index - laneStart (lane);
    if (pos + delta < 0 || pos + delta >= laneSize (lane)) return;
    placeSlot (index, lane, pos + delta);
}

void RigEngine::moveSlotTo (int from, int to)
{
    if (from < 0 || from >= (int) order.size() || to == from) return;
    const int lane = order[(size_t) from]->lane;
    placeSlot (from, lane, juce::jlimit (0, laneSize (lane) - 1, to - laneStart (lane)));
}

void RigEngine::setEnabled (int index, bool on)
{
    if (index < 0 || index >= (int) order.size()) return;
    auto& s = *order[(size_t) index];
    s.enabled = on && s.proc != nullptr;
    s.safeMuted = false;
}

void RigEngine::splitChain()
{
    if (split) return;
    const juce::ScopedLock sl (chainLock());
    split = true;
    wide = true;      // stereo acceso di default: a a sinistra, b a destra
    widen = false; balance = false; gainA = gainB = 1.0f; alignMs = 0.0f;
    std::vector<std::unique_ptr<Slot>> copies;
    for (auto& s : order) { s->lane = 1; if ((int) (order.size() + copies.size()) < kMaxSlots) copies.push_back (cloneSlot (*s)); }
    for (auto& c : copies) { c->lane = 2; if (prepared) prepareSlot (*c); order.push_back (std::move (c)); }
    publish();
    if (onChainChanged) onChainChanged();
}

void RigEngine::mergeChain (int keepLane)
{
    if (! split) return;
    keepLane = keepLane == 2 ? 2 : 1;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    std::vector<std::unique_ptr<Slot>> gone, kept;
    for (auto& s : order)
        if (s->lane == 0 || s->lane == keepLane) { s->lane = 0; kept.push_back (std::move (s)); }
        else gone.push_back (std::move (s));
    order = std::move (kept);
    split = false;
    wide = false;
    widen = false; widenState = 0; balance = false;
    remuteUncovered (covered);
    armCovered();
    publish();
    for (auto& s : gone) retired.back().slots.push_back (std::move (s));
    if (onChainChanged) onChainChanged();
}

void RigEngine::moveSplit (int viewLane, int newTrunk)
{
    if (! split) return;
    viewLane = viewLane == 2 ? 2 : 1;
    const int other = 3 - viewLane, trunk = laneSize (0);
    newTrunk = juce::jlimit (0, trunk + laneSize (viewLane), newTrunk);
    if (newTrunk == trunk) return;
    const juce::ScopedLock sl (chainLock());
    const auto covered = coveredAmps();
    std::vector<std::unique_ptr<Slot>> gone;
    if (newTrunk < trunk)
    {
        // i moduli dopo il nuovo punto di split vanno in testa al ramo guardato, e una copia in testa all'altro
        std::vector<Slot*> moving;
        for (auto& s : order) if (s->lane == 0) moving.push_back (s.get());
        moving.erase (moving.begin(), moving.begin() + newTrunk);
        int pos = 0;
        for (auto* m : moving)
        {
            auto copy = cloneSlot (*m);
            const int at = indexOf (m);
            auto own = std::move (order[(size_t) at]);
            order.erase (order.begin() + at);
            own->lane = viewLane;
            order.insert (order.begin() + laneStart (viewLane) + pos, std::move (own));
            if ((int) order.size() < kMaxSlots)
            {
                copy->lane = other;
                if (prepared) prepareSlot (*copy);
                order.insert (order.begin() + laneStart (other) + pos, std::move (copy));
            }
            ++pos;
        }
    }
    else
    {
        // i primi moduli del ramo guardato entrano nella parte comune; lo stesso tipo nell'altro ramo si toglie
        const int count = newTrunk - trunk;
        std::vector<Slot*> moving;
        for (auto& s : order) if (s->lane == viewLane && (int) moving.size() < count) moving.push_back (s.get());
        for (auto* m : moving)
        {
            for (size_t k = 0; k < order.size(); ++k)
                if (order[k]->lane == other && order[k]->info->id == m->info->id)
                {
                    gone.push_back (std::move (order[k]));
                    order.erase (order.begin() + (long) k);
                    break;
                }
            const int at = indexOf (m);
            auto own = std::move (order[(size_t) at]);
            order.erase (order.begin() + at);
            own->lane = 0;
            order.insert (order.begin() + laneSize (0), std::move (own));
        }
    }
    remuteUncovered (covered);
    armCovered();
    publish();
    for (auto& s : gone) retired.back().slots.push_back (std::move (s));
    if (onChainChanged) onChainChanged();
}

void RigEngine::clear()
{
    const juce::ScopedLock sl (chainLock());
    std::vector<std::unique_ptr<Slot>> gone;
    for (auto& s : order) gone.push_back (std::move (s));
    order.clear();
    split = false;
    wide = false;
    widen = false; widenState = 0; balance = false;
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
    slot (1).enabled = false; slot (1).safeMuted = false;   // b-amp spento (catena dei test: lo si accende a mano)
    onChainChanged = std::move (cb);
    if (onChainChanged) onChainChanged();
}

void RigEngine::armIfCovered (Slot& s)
{
    // b-amp da un preset riparte acceso se dopo di lui c'è un b-blend (senza IR b-blend blocca l'uscita da solo)
    if (cabAfter (indexOf (&s), false)) { s.enabled = true; s.safeMuted = false; }
}

juce::ValueTree RigEngine::saveRig() const
{
    juce::ValueTree t ("BRIG");
    t.setProperty ("version", 2, nullptr);
    if (split)
    {
        t.setProperty ("split", true, nullptr);
        t.setProperty ("wide", wide.load(), nullptr);
        if (widen.load()) { t.setProperty ("widen", true, nullptr); t.setProperty ("widenMs", widenMs.load(), nullptr); }
        if (balance.load())
        {
            t.setProperty ("balance", true, nullptr);
            t.setProperty ("gainA", gainA.load(), nullptr); t.setProperty ("gainB", gainB.load(), nullptr);
            t.setProperty ("alignMs", alignMs.load(), nullptr);
        }
    }
    for (auto& s : order)
    {
        juce::ValueTree c ("SLOT");
        c.setProperty ("id", s->info->id, nullptr);
        c.setProperty ("enabled", s->enabled.load(), nullptr);
        if (s->safeMuted.load()) c.setProperty ("safeMuted", true, nullptr);
        if (s->lane != 0) c.setProperty ("lane", s->lane, nullptr);
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
    split = (bool) t.getProperty ("split", false);
    wide = split && (bool) t.getProperty ("wide", true);
    widen = split && (bool) t.getProperty ("widen", false);
    widenMs = (float) t.getProperty ("widenMs", 0.0f);
    balance = split && (bool) t.getProperty ("balance", false);
    gainA = (float) t.getProperty ("gainA", 1.0f); gainB = (float) t.getProperty ("gainB", 1.0f);
    alignMs = (float) t.getProperty ("alignMs", 0.0f);
    std::vector<Slot*> armed;
    for (auto c : t)
    {
        const int lane = split ? juce::jlimit (0, 2, (int) c.getProperty ("lane", 0)) : 0;
        if (! canPlace (c["id"].toString(), lane)) continue;   // file modificato a mano: un modulo per tipo per ramo
        auto* s = addModuleAt (c["id"].toString(), lane, laneSize (lane));
        if (s == nullptr) continue;
        if (s->proc && c.hasProperty ("state"))
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (c["state"].toString())) s->proc->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        const bool on = (bool) c.getProperty ("enabled", true);
        s->enabled = s->proc != nullptr && ! s->info->safeArm && on;
        s->safeMuted = false;
        if (s->info->safeArm && s->proc != nullptr)
        {
            // acceso nel preset (o muto per protezione): riparte acceso solo con la cassa; spento dall'utente: resta spento.
            // I preset di prima (versione 1) non distinguono: un ampli spento mostra "safe muted" ma resta spento.
            if (on || (bool) c.getProperty ("safeMuted", false)) { s->safeMuted = true; armed.push_back (s); }
            else if ((int) t.getProperty ("version", 1) < 2) s->safeMuted = true;
        }
    }
    for (auto* s : armed) armIfCovered (*s);
    onChainChanged = std::move (cb);
    if (onChainChanged) onChainChanged();
}

// ---------------------------------------------------------------- audio

void RigEngine::runProcessor (Slot& s, juce::AudioBuffer<float>& w, int n) noexcept
{
    float* ch[2] = { w.getWritePointer (0), w.getWritePointer (1) };
    if (s.inCh == 1)   // modulo mono: somma L/R (identica se il segnale è ancora mono)
        for (int i = 0; i < n; ++i) ch[0][i] = 0.5f * (ch[0][i] + ch[1][i]);
    juce::AudioBuffer<float> buf (ch, juce::jmax (s.inCh, s.outCh), n);
    midi.clear();
    s.proc->processBlock (buf, midi);
    if (s.outCh == 1) juce::FloatVectorOperations::copy (ch[1], ch[0], n);
    s.processedBlocks.fetch_add (1, std::memory_order_relaxed);
}

void RigEngine::processSlot (Slot& s, juce::AudioBuffer<float>& w, int n) noexcept
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
    if (s.fade >= 1.0f && tgt) { runProcessor (s, w, n); return; }

    // cambio di stato: crossfade cosinusoidale tra il segnale senza e con il modulo
    for (int c = 0; c < 2; ++c) s.dry.copyFrom (c, 0, w, c, 0, n);
    runProcessor (s, w, n);
    float* w0 = w.getWritePointer (0);
    float* w1 = w.getWritePointer (1);
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

void RigEngine::startMeasure (int what)
{
    measureWhat = measureWhat.load() | what;
    if (widenState.load() == 0) { capPos = 0; measureProg = 0.0f; widenState = 1; }   // il thread audio non registra finché widenState non è 1
}

RigEngine::BalanceResult RigEngine::measureBalance (const float* a, const float* b, int n, double sr)
{
    // b registrato con lead campioni in più prima di a (vedi bestWidenLag)
    const int lead = (int) (0.026 * sr), lim = (int) (0.005 * sr), m = n - lim;
    BalanceResult r;
    double best = 0.0; int bestL = 0;
    for (int L = -lim; L <= lim; ++L)
    {
        const float* bb = b + lead - L;
        double c = 0.0;
        for (int i = 0; i < m; ++i) c += (double) a[i] * bb[i];
        if (std::abs (c) > std::abs (best)) { best = c; bestL = L; }
    }
    double ea = 0.0, eb = 0.0;
    for (int i = 0; i < m; ++i) { ea += (double) a[i] * a[i]; eb += (double) b[lead + i] * b[lead + i]; }
    const double ra = std::sqrt (ea / m), rb = std::sqrt (eb / m), target = std::sqrt (ra * rb);
    r.alignMs = (float) (1000.0 * bestL / sr);
    r.gainA = ra > 1.0e-9 ? (float) juce::jlimit (0.25, 4.0, target / ra) : 1.0f;
    r.gainB = rb > 1.0e-9 ? (float) juce::jlimit (0.25, 4.0, target / rb) : 1.0f;
    if (best < 0.0) r.gainB = -r.gainB;   // b in controfase: si inverte
    return r;
}

float RigEngine::bestWidenLag (const float* a, const float* b, int n, double sr)
{
    // b registrato con lead campioni in più prima di a: b[lead + i - L] è b ritardato di L rispetto ad a[i]
    const int lead = (int) (0.026 * sr), lo = (int) (0.005 * sr), hi = juce::jmin (lead, (int) (0.025 * sr));
    double best = -1.0e300; int bestL = (lo + hi) / 2;
    for (int L = lo; L <= hi; ++L)
    {
        const float* bb = b + lead - L;
        double c = 0.0;
        for (int i = 0; i < n; ++i) c += (double) a[i] * bb[i];
        if (c > best) { best = c; bestL = L; }
    }
    return (float) (1000.0 * bestL / sr);
}

int RigEngine::pollMeasure()
{
    if (widenState.load() != 2) return 0;
    const int what = measureWhat.load();
    if (what & kWiden) { widenMs = bestWidenLag (capA.data(), capB.data(), (int) capA.size(), fs); widen = true; }
    if (what & kBalance)
    {
        const auto r = measureBalance (capA.data(), capB.data(), (int) capA.size(), fs);
        alignMs = r.alignMs; gainA = r.gainA; gainB = r.gainB;
        balance = true;
    }
    measureWhat = 0;
    widenState = 0;
    return what;
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
            // il pitch si legge solo all'inizio del click: cambiarlo non spezza mai un suono a metà
            const double pitch = std::exp2 (juce::jlimit (-12.0f, 12.0f, metroPitch.load (std::memory_order_relaxed)) / 12.0);
            const double f = pitch * (clickSound == 2 ? (beat == 0 ? 1250.0 : 950.0) : (beat == 0 ? 2000.0 : 1300.0));
            clickInc = (float) (juce::MathConstants<double>::twoPi * f / fs);
            clickInc2 = clickInc * 2.76f;   // seconda risonanza del legno
            clickPhase = clickPhase2 = 0;
            clickDecay = decays[(size_t) clickSound];
            clickEnv = (beat == 0 ? 0.9f : 0.6f) * juce::jlimit (0.0f, 1.0f, metroVolume.load (std::memory_order_relaxed));   // il doppio di prima al massimo
            hatMix = (float) juce::jlimit (0.0, 1.0, 0.5 + juce::jlimit (-12.0f, 12.0f, metroPitch.load (std::memory_order_relaxed)) / 24.0);
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
                // pitch: più alto = più differenza seconda (più chiaro), più basso = più differenza prima (più scuro)
                v = clickEnv * (hatMix * (nz - 2.0f * clickPrev + clickPrev2) * 0.35f + (1.0f - hatMix) * (nz - clickPrev) * 0.5f);
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

        // playhead: in una DAW quello del progetto; nell'app i bpm del metronomo per i moduli sincronizzati al tempo
        if (hostPlayHead != nullptr)
        {
            if (const auto pos = hostPlayHead->getPosition()) playHead.info = *pos;
        }
        else
        {
        playHead.info.setBpm (juce::jlimit (kMinBpm, kMaxBpm, bpm.load (std::memory_order_relaxed)));
        const auto sig = kSigs[(size_t) juce::jlimit (0, kNumSigs - 1, sigIndex.load (std::memory_order_relaxed))];
        playHead.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { sig.first, sig.second });
        playHead.info.setIsPlaying (metroOn.load (std::memory_order_relaxed));
        playHead.info.setPpqPosition (ppq);
        }

        if (! chain->split)
            for (int k = 0; k < chain->n; ++k) processSlot (*chain->s[(size_t) k], work, n);
        else
        {
            // prima dello split, poi i due rami in parallelo e la somma
            for (int k = 0; k < chain->nTrunk; ++k) processSlot (*chain->s[(size_t) k], work, n);
            for (int c = 0; c < 2; ++c) workB.copyFrom (c, 0, work, c, 0, n);
            for (int k = chain->nTrunk; k < chain->nTrunk + chain->nA; ++k) processSlot (*chain->s[(size_t) k], work, n);
            for (int k = chain->nTrunk + chain->nA; k < chain->n; ++k) processSlot (*chain->s[(size_t) k], workB, n);
            // fine dei rami: misura (registra a e b così come escono), poi ritardo e guadagno di ogni ramo
            {
                float* aw[2] = { w0, w1 };
                float* bw[2] = { workB.getWritePointer (0), workB.getWritePointer (1) };
                if (widenState.load (std::memory_order_relaxed) == 1 && ! capA.empty())
                {
                    const int lead = (int) capB.size() - (int) capA.size();
                    for (int i = 0; i < n && capPos < (int) capB.size(); ++i, ++capPos)
                    {
                        capB[(size_t) capPos] = 0.5f * (bw[0][i] + bw[1][i]);
                        if (capPos >= lead) capA[(size_t) (capPos - lead)] = 0.5f * (aw[0][i] + aw[1][i]);
                    }
                    measureProg.store ((float) capPos / (float) capB.size(), std::memory_order_relaxed);
                    if (capPos >= (int) capB.size())
                    {
                        double e = 0; for (float v : capA) e += (double) v * v;
                        if (e / (double) capA.size() > 1.0e-5) widenState.store (2);   // si è suonato (sopra -50 dBFS rms)
                        capPos = 0;   // altrimenti si riprova
                    }
                }
                // ritardi voluti (campioni): in stereo widen su b; in mono l'allineamento di balance
                const bool st = wide.load (std::memory_order_relaxed), bal = balance.load (std::memory_order_relaxed);
                const float msToS = 0.001f * (float) fs, maxD = (float) (kWidenBuf - 4);
                float want[2] = { 0.0f, 0.0f };
                if (st) { if (widen.load (std::memory_order_relaxed)) want[1] = widenMs.load (std::memory_order_relaxed) * msToS; }
                else if (bal) { const float al = alignMs.load (std::memory_order_relaxed) * msToS; want[al > 0.0f ? 1 : 0] = std::abs (al); }
                const float gWant[2] = { bal ? gainA.load (std::memory_order_relaxed) : 1.0f, bal ? gainB.load (std::memory_order_relaxed) : 1.0f };
                for (int l = 0; l < 2; ++l)
                {
                    want[l] = juce::jlimit (0.0f, maxD, want[l]);
                    float** ch = l == 0 ? aw : bw;
                    for (int i = 0; i < n; ++i)
                    {
                        delayCur[l] += widenSmooth * (want[l] - delayCur[l]);
                        gainCur[l] += widenSmooth * (gWant[l] - gainCur[l]);
                        if (want[l] <= 0.0f && delayCur[l] < 0.01f) delayCur[l] = 0.0f;
                        const float pos = (float) widenW + (float) i - delayCur[l] + (float) kWidenBuf;
                        const int i0 = (int) pos; const float fr = pos - (float) i0;
                        for (int c = 0; c < 2; ++c)
                        {
                            auto& ln = laneLine[(size_t) (2 * l + c)];
                            ln[(size_t) ((widenW + i) & (kWidenBuf - 1))] = ch[c][i];   // la linea resta sempre aggiornata
                            if (delayCur[l] > 0.0f)
                            {
                                const float a0 = ln[(size_t) (i0 & (kWidenBuf - 1))], a1 = ln[(size_t) ((i0 + 1) & (kWidenBuf - 1))];
                                ch[c][i] = a0 + fr * (a1 - a0);
                            }
                            ch[c][i] *= gainCur[l];
                        }
                    }
                }
                widenW = (widenW + n) & (kWidenBuf - 1);
            }
            // somma: a -6 dB ciascuno (due rami uguali = un ramo solo); stereo: a a sinistra, b a destra (in mono).
            // Il passaggio tra le due somme dura 20 ms.
            const float* b0 = workB.getReadPointer (0);
            const float* b1 = workB.getReadPointer (1);
            const float wideTarget = wide.load (std::memory_order_relaxed) ? 1.0f : 0.0f, step = (float) (1.0 / (0.02 * fs));
            for (int i = 0; i < n; ++i)
            {
                wideMix = wideTarget > wideMix ? juce::jmin (wideTarget, wideMix + step) : juce::jmax (wideTarget, wideMix - step);
                const float sumL = 0.5f * (w0[i] + b0[i]), sumR = 0.5f * (w1[i] + b1[i]);
                const float aM = 0.5f * (w0[i] + w1[i]), bM = 0.5f * (b0[i] + b1[i]);
                w0[i] = sumL + wideMix * (aM - sumL);
                w1[i] = sumR + wideMix * (bM - sumR);
            }
        }

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
        // somma con un limite morbido sopra 0,85: un click forte su una chitarra forte non gracchia
        if (metronome (n))
        {
            auto soft = [] (float x) noexcept
            {
                const float a = std::abs (x);
                if (a <= 0.85f) return x;
                return std::copysign (0.85f + 0.149f * std::tanh ((a - 0.85f) / 0.149f), x);
            };
            for (int i = 0; i < n; ++i)
            {
                w0[i] = soft (w0[i] + metroBuf[(size_t) i]);
                w1[i] = soft (w1[i] + metroBuf[(size_t) i]);
            }
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
