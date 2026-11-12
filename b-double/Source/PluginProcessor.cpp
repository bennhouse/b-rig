#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr float kMinDelayMs = 6.0f, kMaxDelayMs = 40.0f, kWindowMs = 10.0f, kXfadeMs = 40.0f;
}

BDoubleProcessor::BDoubleProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BDouble", createLayout())
{
    pMode = apvts.getRawParameterValue (BDoubleIDs::mode);
    pTight = apvts.getRawParameterValue (BDoubleIDs::tightness);
    pVar = apvts.getRawParameterValue (BDoubleIDs::variation);
    pSafe = apvts.getRawParameterValue (BDoubleIDs::monoSafe);
    pActive = apvts.getRawParameterValue (BDoubleIDs::active);
    pDelay = apvts.getRawParameterValue (BDoubleIDs::delay);
    pDetune = apvts.getRawParameterValue (BDoubleIDs::detune);
    pDynamic = apvts.getRawParameterValue (BDoubleIDs::dynamic);
}

juce::AudioProcessorValueTreeState::ParameterLayout BDoubleProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BDoubleIDs::mode, 1 }, "Mode", StringArray { "Hard-pan", "Center-wide" }, 0));
    auto pct = [] (const char* id, const char* name, float def)
    { return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 1.0f), def,
                                                    AudioParameterFloatAttributes().withLabel ("%")); };
    l.add (pct (BDoubleIDs::tightness, "Tightness", 50.0f));
    l.add (pct (BDoubleIDs::variation, "Variation", 50.0f));
    l.add (pct (BDoubleIDs::monoSafe, "Mono-safe", 0.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDoubleIDs::active, 1 }, "Active", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDoubleIDs::delay, 1 }, "Delay", NormalisableRange<float> (kMinDelayMs, kMaxDelayMs, 0.1f), 16.0f,
                                                  AudioParameterFloatAttributes().withLabel ("ms")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BDoubleIDs::detune, 1 }, "Detune", NormalisableRange<float> (0.0f, 20.0f, 0.1f), 6.0f,
                                                  AudioParameterFloatAttributes().withLabel ("cents")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDoubleIDs::dynamic, 1 }, "Dynamic", false));
    return l;
}

bool BDoubleProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BDoubleProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    int size = 1;
    while (size < (int) (0.09 * fs)) size <<= 1;   // ritardo massimo + jitter + mezza finestra, a potenza di 2
    line.assign ((size_t) size, 0.0f);             // buffer azzerato: niente residui all'avvio
    mask = size - 1; pos = 0;
    dryDelay = juce::roundToInt (0.002 * fs);
    fA = sp::coefMs (0.5, fs); fR = sp::coefMs (20.0, fs); sA = sp::coefMs (20.0, fs); sR = sp::coefMs (150.0, fs);
    centsSmooth = 1.0f - sp::coefMs (40.0, fs);
    sustainCoef = 1.0f - sp::coefMs (100.0, fs);
    rotC = (float) std::cos (juce::MathConstants<double>::twoPi * 0.35 / fs);
    rotS = (float) std::sin (juce::MathConstants<double>::twoPi * 0.35 / fs);
    lfoS = 0.0f; lfoC = 1.0f;
    windowSamples = (float) (kWindowMs * 0.001 * fs);
    xfInc = (float) (1.0 / (kXfadeMs * 0.001 * fs));
    tiltCoef = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 1200.0 / fs);
    const double apHz[4] = { 180.0, 650.0, 1900.0, 4200.0 };
    const float d0 = pDelay->load() * 0.001f * (float) fs;
    for (int i = 0; i < 2; ++i)
    {
        auto& v = voices[(size_t) i];
        v = {};
        for (int k = 0; k < 4; ++k) v.ap[k].set (fs, apHz[k]);
        v.varAp[0].set (fs, i == 0 ? 900.0 : 1100.0); v.varAp[1].set (fs, i == 0 ? 2600.0 : 3100.0);
        v.guardAp.set (fs, 200.0);
        v.side = i == 0 ? -1.0f : 1.0f;
        v.curDelay = v.newDelay = d0; v.xf = 1.0f;
        v.phase = 0.5f;   // una sola testa attiva (peso 1): nessun pettine a detune 0
        v.cents = v.tCents = 0.0f;
    }
    gHpD = {}; gHpD.set (sp::Bq::HP, fs, 80.0, 0.7071); gHpW = gHpD;
    gLpD = {}; gLpD.set (sp::Bq::LP, fs, 600.0, 0.7071); gLpW = gLpD;
    gDW = gDD = gWW = 0; gCount = gCross = 0; gPrevD = 0; guardActive.store (false);
    wetCoef = 1.0f - sp::coefMs (20.0, fs);
    wetMix = pActive->load() > 0.5f ? 1.0f : 0.0f;
    alignIn = -1;
    eFast = eSlow = 0; holdoff = 0; sinceAttack = 0; sustainGain = 1; lastSafe = -1; lastManual = -1; lastDynamic = pDynamic->load() > 0.5f;
}

// Pitch shifter: due teste attorno alla presa "baseSamples" (± mezza finestra), pesi sin^2 / cos^2 a somma 1.
float BDoubleProcessor::heads (const Voice& v, float baseSamples) const noexcept
{
    // peso della prima testa: sin^2(pi ph) (Bhaskara, senza trigonometria); la seconda, mezzo giro dopo, ha
    // cos^2 = 1 - sin^2: la somma resta 1 esatto per costruzione
    const float ph0 = v.phase, q = ph0 * (1.0f - ph0);
    const float s0 = 16.0f * q / (5.0f - 4.0f * q);
    const float w0 = s0 * s0;
    float y = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        float ph = ph0 + 0.5f * (float) k;
        ph -= std::floor (ph);
        const float w = k == 0 ? w0 : 1.0f - w0;
        if (w < 1.0e-6f) continue;   // testa a peso nullo: niente calcolo
        const float d = baseSamples + (ph - 0.5f) * windowSamples;
        const int di = (int) d;
        const float t = d - (float) di;
        auto at = [&] (int o) { return line[(size_t) ((pos - di + o) & mask)]; };
        const float xm1 = at (1), x0 = at (0), x1 = at (-1), x2 = at (-2);   // Hermite a 4 punti
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        y += w * (((c3 * t + c2) * t + c1) * t + x0);
    }
    return y;
}

float BDoubleProcessor::render (Voice& v) noexcept
{
    const float ratio = std::exp2 (v.cents * (1.0f / 1200.0f));
    v.phase += (1.0f - ratio) / windowSamples;   // la testa scorre: intonazione
    v.phase -= std::floor (v.phase);
    const float lfo = (pDynamic->load (std::memory_order_relaxed) > 0.5f ? (0.05f + 0.35f * (1.0f - pTight->load (std::memory_order_relaxed) / 100.0f)) : 0.0f)
                    * 0.001f * (float) fs * lfoS * v.side;
    if (v.xf >= 1.0f) return heads (v, v.curDelay + lfo);
    // cambio di presa: crossfade coseno tra la presa vecchia e la nuova (nessun glissato, nessun click)
    const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * v.xf);
    const float y = heads (v, v.curDelay + lfo) * (1.0f - w) + heads (v, v.newDelay + lfo) * w;
    v.xf += xfInc;
    if (v.xf >= 1.0f) { v.xf = 1.0f; v.curDelay = v.newDelay; }
    return y;
}

void BDoubleProcessor::moveTo (Voice& v, float delaySamples) noexcept
{
    const float lim = juce::jlimit (kMinDelayMs * 0.001f * (float) fs, (kMaxDelayMs + 8.0f) * 0.001f * (float) fs, delaySamples);
    if (v.xf < 1.0f || std::abs (lim - v.curDelay) < 0.5f) return;   // crossfade in corso o stessa presa
    v.newDelay = lim; v.xf = 0.0f;
}

// Presa vicino a targetSamples in cui l'ultima nota è meno correlata con sé stessa (offsetSamples: ritardo
// dell'altro canale, 2 ms del dry in hard-pan). Ultimi 1024 campioni, passo di 2 campioni e poi rifinitura.
float BDoubleProcessor::leastCorrelatedDelay (float targetSamples, float offsetSamples) const
{
    const int n = 1024, range = (int) (0.0015 * fs);
    const int centre = (int) (targetSamples - offsetSamples);
    double e0 = 0; for (int i = 0; i < n; ++i) { const float v = line[(size_t) ((pos - i) & mask)]; e0 += (double) v * v; }
    if (e0 < 1e-8) return targetSamples;
    auto corr = [&] (int lag) { double r = 0; for (int i = 0; i < n; ++i) r += (double) line[(size_t) ((pos - i) & mask)] * line[(size_t) ((pos - i - lag) & mask)]; return std::abs (r); };
    double bestAbs = 1e30; int bestLag = centre;
    for (int lag = juce::jmax (1, centre - range); lag <= centre + range; lag += 2)
        if (const double r = corr (lag); r < bestAbs) { bestAbs = r; bestLag = lag; }
    for (int lag : { bestLag - 1, bestLag + 1 })
        if (lag >= 1) if (const double r = corr (lag); r < bestAbs) { bestAbs = r; bestLag = lag; }
    return (float) bestLag + offsetSamples;
}

float BDoubleProcessor::post (Voice& v, float y, float ms, float var) noexcept
{
    // variation: tilt opposto sulle due doppie e due passa-tutto mescolati (timbro di un'altra chitarra)
    v.tiltLp += tiltCoef * (y - v.tiltLp);
    y = v.tiltLp * v.gLo + (y - v.tiltLp) * v.gHi;
    if (var > 0.0f) { const float a = v.varAp[1].process (v.varAp[0].process (y)); y += 0.5f * var * (a - y); }
    if (ms > 0.0f)
    {
        float a = y;
        for (auto& f : v.ap) a = f.process (a);                  // rotazione di fase senza cambiare timbro
        y = y + ms * (a - y);
        y = v.hp.process (y);
    }
    // phase guard: 90° di rotazione sulla frequenza dominante quando la doppia va in controfase
    const float gtar = guardActive.load (std::memory_order_relaxed) ? 1.0f : 0.0f;
    v.guardMix += 0.001f * (gtar - v.guardMix);
    if (v.guardMix > 1.0e-3f) { const float g = v.guardAp.process (y); y += v.guardMix * (g - y); }
    return y;
}

void BDoubleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn = getTotalNumInputChannels();
    const int n = buffer.getNumSamples();
    const bool wide = pMode->load() > 0.5f, dynamic = pDynamic->load() > 0.5f;
    const float tight = pTight->load() / 100.0f, var = pVar->load() / 100.0f, ms = pSafe->load() / 100.0f;
    const float baseMs = pDelay->load(), det = pDetune->load();
    const float msToS = 0.001f * (float) fs;
    if (std::abs (ms - lastSafe) > 1e-4f) { for (auto& v : voices) v.hp.set (sp::Bq::HP, fs, 20.0 + 280.0 * ms, 0.7071); lastSafe = ms; }
    const int holdSamples = (int) (0.06 * fs), sustainAfter = (int) (0.3 * fs);
    const float sustainTarget = sp::dbToGain (-6.0f * ms);
    for (auto& v : voices) { const float tiltDb = 4.5f * var * v.side; v.gLo = sp::dbToGain (-0.5f * tiltDb); v.gHi = sp::dbToGain (0.5f * tiltDb); }

    // ritardo di ogni doppia senza umanizzazione: hard-pan = delay, center-wide = delay ∓ 3 ms
    auto manualMs = [&] (int vi) { return wide ? baseMs + (vi == 0 ? -3.0f : 3.0f) : baseMs; };
    auto manualCents = [&] (int vi) { return wide ? (vi == 0 ? -det : det) : det; };
    const float off = wide ? 0.0f : (float) dryDelay;
    if (! dynamic && (std::abs (baseMs - lastManual) > 0.05f || lastDynamic))
    {
        // manuale: prese fisse sul valore scelto (cambio = crossfade), intonazione fissa
        bool fading = false; for (auto& v : voices) fading = fading || v.xf < 1.0f;
        if (! fading) { for (int vi = 0; vi < 2; ++vi) moveTo (voices[(size_t) vi], juce::jmax (kMinDelayMs, manualMs (vi)) * msToS); lastManual = baseMs; }
        if (! wide) { auto& v0 = voices[0]; v0.xf = 1.0f; v0.curDelay = v0.newDelay; }
    }
    if (! dynamic) { for (int vi = 0; vi < 2; ++vi) voices[(size_t) vi].tCents = manualCents (vi); alignIn = -1; guardActive.store (false); }
    lastDynamic = dynamic;
    // in hard-pan la doppia sinistra non suona: i suoi cambi di presa si chiudono subito (non bloccano gli altri)
    auto settleUnused = [&] { if (! wide) { auto& v0 = voices[0]; v0.xf = 1.0f; v0.curDelay = v0.newDelay; } };
    settleUnused();

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float x = numIn > 1 ? 0.5f * (L[i] + R[i]) : L[i];
        line[(size_t) pos] = x;

        // 1. plettrata -> (dynamic) nuove prese e intonazioni casuali attorno ai valori scelti
        const float a = std::abs (x);
        eFast = a > eFast ? fA * (eFast - a) + a : fR * (eFast - a) + a;
        eSlow = a > eSlow ? sA * (eSlow - a) + a : sR * (eSlow - a) + a;
        if (holdoff > 0) --holdoff;
        if (eFast > 2.0f * eSlow && eFast > 1.0e-4f && holdoff == 0)
        {
            if (dynamic)
            {
                const float jitterMs = 0.5f + 7.5f * (1.0f - tight);   // tightness: ampiezza del jitter temporale
                for (int vi = 0; vi < 2; ++vi)
                {
                    auto& v = voices[(size_t) vi];
                    pendingDelay[(size_t) vi] = (manualMs (vi) + (2.0f * rng.nextFloat() - 1.0f) * jitterMs) * msToS;
                    v.tCents = manualCents (vi) * (0.5f + 0.5f * rng.nextFloat()) + (2.0f * rng.nextFloat() - 1.0f) * 4.0f * (1.0f - tight);
                }
                alignIn = (int) (0.03 * fs);   // tra 30 ms la nota nuova è nel buffer: si sceglie la presa meno correlata
            }
            holdoff = holdSamples;
            sinceAttack = 0;
        }
        else ++sinceAttack;
        if (alignIn > 0 && --alignIn == 0)
        {
            bool fading = false; for (auto& v : voices) fading = fading || v.xf < 1.0f;
            if (fading) alignIn = 1;   // si aspetta la fine del crossfade in corso
            else
            {
                for (int vi = 0; vi < 2; ++vi) moveTo (voices[(size_t) vi], leastCorrelatedDelay (pendingDelay[(size_t) vi], off));
                settleUnused();
                alignIn = -1;
            }
        }
        for (auto& v : voices) v.cents += centsSmooth * (v.tCents - v.cents);
        { const float s2 = lfoS * rotC + lfoC * rotS; lfoC = lfoC * rotC - lfoS * rotS; lfoS = s2; }

        // mono-safe: nei passaggi tenuti (nessun attacco da 300 ms) la doppia scende fino a -6 dB
        sustainGain += sustainCoef * ((sinceAttack > sustainAfter ? sustainTarget : 1.0f) - sustainGain);

        const float wetR = post (voices[1], render (voices[1]), ms, var) * sustainGain;
        float outL, outR, dryRef;
        if (wide)
        {
            const float wetL = post (voices[0], render (voices[0]), ms, var) * sustainGain;
            outL = 0.70710678f * (x + wetL);
            outR = 0.70710678f * (x + wetR);
            dryRef = x;
        }
        else
        {
            outL = line[(size_t) ((pos - dryDelay) & mask)];   // dry ritardato di 2 ms
            outR = wetR;
            dryRef = outL;
        }

        // 5. phase guard (dynamic): correlazione dry / doppia sulle fondamentali, a finestre di 2048 campioni
        if (dynamic)
        {
            const float bd = gLpD.process (gHpD.process (dryRef)), bw = gLpW.process (gHpW.process (wetR));
            gDW += (double) bd * bw; gDD += (double) bd * bd; gWW += (double) bw * bw;
            if ((bd >= 0.0f) != (gPrevD >= 0.0f)) ++gCross;
            gPrevD = bd;
            if (++gCount >= 2048)
            {
                if (gDD > 1.0e-7 && gWW > 1.0e-7)
                {
                    const double rho = gDW / std::sqrt (gDD * gWW);
                    if (rho < -0.3)
                    {
                        const double fDom = juce::jlimit (60.0, 800.0, 0.5 * gCross * fs / 2048.0);
                        for (auto& v : voices) v.guardAp.set (fs, fDom);
                        guardActive.store (true);
                    }
                    else if (rho > 0.1) guardActive.store (false);
                }
                gDW = gDD = gWW = 0; gCount = gCross = 0;
            }
        }

        // on/off: spento = il segnale originale su entrambi i canali (sfumato in 20 ms); il goniometro resta attivo
        wetMix += wetCoef * ((pActive->load (std::memory_order_relaxed) > 0.5f ? 1.0f : 0.0f) - wetMix);
        L[i] = x + wetMix * (outL - x);
        R[i] = x + wetMix * (outR - x);
        pos = (pos + 1) & mask;

        if (++scopeDecim >= 2)
        {
            scopeDecim = 0;
            const int p = (scopePos.load (std::memory_order_relaxed) + 1) & (kScope - 1);
            scope[(size_t) p] = { L[i], R[i] };
            scopePos.store (p, std::memory_order_release);
        }
    }
    { const float m = 1.0f / std::sqrt (lfoS * lfoS + lfoC * lfoC); lfoS *= m; lfoC *= m; }
    idle.store (sinceAttack > (int) (0.5 * fs) && eSlow < 1.0e-4f);
    const auto& vr = voices[1];
    shownDelayMs.store ((vr.xf < 1.0f ? vr.newDelay : vr.curDelay) / msToS);
    shownCents.store (vr.cents);
    for (auto& v : voices) v.hp.flush();
    for (auto* f : { &gHpD, &gLpD, &gHpW, &gLpW }) f->flush();
}

void BDoubleProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BDoubleProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BDoubleProcessor::createEditor() { return new BDoubleEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BDoubleProcessor(); }
