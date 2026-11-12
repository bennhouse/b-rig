#include "PluginProcessor.h"
#include "PluginEditor.h"

BAdaptProcessor::BAdaptProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BAdapt", createLayout())
{
    pSafe = apvts.getRawParameterValue (BAdaptIDs::safeWidth);
    pThump = apvts.getRawParameterValue (BAdaptIDs::thump);
    pHarsh = apvts.getRawParameterValue (BAdaptIDs::deharsh);
    pMonitor = apvts.getRawParameterValue (BAdaptIDs::monitor);
}

juce::AudioProcessorValueTreeState::ParameterLayout BAdaptProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (const char* id, const char* name, float def)
    { return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 1.0f), def,
                                                    AudioParameterFloatAttributes().withLabel ("%")); };
    l.add (pct (BAdaptIDs::safeWidth, "Safe width", 50.0f));
    l.add (pct (BAdaptIDs::thump, "Thump", 0.0f));
    l.add (pct (BAdaptIDs::deharsh, "De-harsh", 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BAdaptIDs::monitor, 1 }, "Monitor", StringArray { "Off", "Phone", "Laptop" }, 0));
    return l;
}

bool BAdaptProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BAdaptProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs = sampleRate;
    for (auto& c : ch)
    {
        c = {};
        c.bassHp.set (sp::Bq::HP, fs, 75.0, 0.7071);  c.bassLp.set (sp::Bq::LP, fs, 155.0, 0.7071);
        c.harmHp.set (sp::Bq::HP, fs, 170.0, 0.7071); c.harmLp.set (sp::Bq::LP, fs, 420.0, 0.7071);
        c.codec.set (sp::Bq::LP, fs, 13500.0, 0.7071);
    }
    sideHp = {}; sideHp.set (sp::Bq::HP, fs, 250.0, 0.7071);
    for (int b = 0; b < kDyn; ++b)
    {
        scBand[(size_t) b] = {}; scBand[(size_t) b].set (sp::Bq::BP, fs, kDynHz[b], kDynQ);
        for (auto& c : ch) { c.dyn[(size_t) b].set (fs, kDynHz[b], kDynQ); c.dyn[(size_t) b].reset(); }
        bandEnv[(size_t) b] = 0.0f; bandG[(size_t) b] = bandGTarget[(size_t) b] = 1.0f;
    }
    lastAmt = -1.0f;
    corrCoef = 1.0f - sp::coefMs (50.0, fs);
    sCoef = sp::coefMs (20.0, fs);
    eAtk = sp::coefMs (1.0, fs); eRel = sp::coefMs (40.0, fs);
    dcCoef = 1.0f - sp::coefMs (1000.0 / (2.0 * 3.14159 * 10.0), fs);   // ~10 Hz
    cLR = cLL = cRR = env = 0; sGain = 1; curCutDb = 0; appliedCutDb = 100; lastMonitor = -1;

    os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
    tpDetect = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    tpDetect->initProcessing ((size_t) juce::jmax (1, samplesPerBlock));
    detLag = juce::roundToInt (tpDetect->getLatencyInSamples() * 0.5f) + 1;   // solo andata
    detBuf.setSize (2, juce::jmax (1, samplesPerBlock));
    for (auto& l : look) l.fill (0.0f);
    gReq.fill (1.0f);
    lookPos = 0; limG = 1.0f;
    limRel = 1.0f - sp::coefMs (50.0, fs);
    setLatencySamples (juce::roundToInt (os->getLatencyInSamples()) + kLook);
}

float BAdaptProcessor::responseDb (double hz) const
{
    double mag = 1.0;
    sp::Bq b;
    const double t = pThump->load() / 100.0;
    if (t > 0.0)   // armoniche aggiunte: indicativo, banda 170-420 Hz
    {
        sp::Bq h, lo; h.set (sp::Bq::HP, fs, 170.0, 0.7071); lo.set (sp::Bq::LP, fs, 420.0, 0.7071);
        mag *= 1.0 + 3.0 * t * h.magnitude (hz, fs) * lo.magnitude (hz, fs);
    }
    const double amt = pHarsh->load() / 100.0;
    for (int k = 0; k < 2; ++k) { b.set (sp::Bq::Peak, fs, kStaticHz[k], kStaticQ[k], -kStaticDb[k] * amt); mag *= b.magnitude (hz, fs); }
    for (int k = 0; k < kDyn; ++k)
        if (const float c = dynCutDb[(size_t) k].load(); c > 0.01f) { b.set (sp::Bq::Peak, fs, kDynHz[k], kDynQ, -c); mag *= b.magnitude (hz, fs); }
    b.set (sp::Bq::LP, fs, 13500.0, 0.7071); mag *= b.magnitude (hz, fs);
    const int m = (int) pMonitor->load();
    if (m == 1)
    {
        b.set (sp::Bq::HP, fs, 400.0, 0.5412); mag *= b.magnitude (hz, fs); b.set (sp::Bq::HP, fs, 400.0, 1.3066); mag *= b.magnitude (hz, fs);
        b.set (sp::Bq::Peak, fs, 2800.0, 1.2, 5.0); mag *= b.magnitude (hz, fs);
        b.set (sp::Bq::LP, fs, 9000.0, 0.5412); mag *= b.magnitude (hz, fs); b.set (sp::Bq::LP, fs, 9000.0, 1.3066); mag *= b.magnitude (hz, fs);
    }
    else if (m == 2)
    {
        b.set (sp::Bq::HP, fs, 180.0, 0.5412); mag *= b.magnitude (hz, fs); b.set (sp::Bq::HP, fs, 180.0, 1.3066); mag *= b.magnitude (hz, fs);
        b.set (sp::Bq::Peak, fs, 2200.0, 1.2, 3.5); mag *= b.magnitude (hz, fs);
        b.set (sp::Bq::LP, fs, 12000.0, 0.7071); mag *= b.magnitude (hz, fs);
    }
    return (float) (20.0 * std::log10 (juce::jmax (mag, 1e-6)));
}

void BAdaptProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numCh = juce::jmin (2, buffer.getNumChannels(), getTotalNumInputChannels());
    const int n = buffer.getNumSamples();
    // range estesi (0-100 % resta, l'effetto arriva più lontano): side fino a -12 dB,
    // armoniche fino a 6x, taglio dinamico fino a 24 dB con soglia che scende fino a -48 dB
    const float safe = pSafe->load() / 100.0f, thumpG = 6.0f * pThump->load() / 100.0f, amt = pHarsh->load() / 100.0f;
    if (std::abs (amt - lastAmt) > 1.0e-4f)   // EQ fisso dei punti caldi (70 % del lavoro)
    {
        for (auto& c : ch) { c.stat1.set (sp::Bq::Peak, fs, kStaticHz[0], kStaticQ[0], -kStaticDb[0] * amt); c.stat2.set (sp::Bq::Peak, fs, kStaticHz[1], kStaticQ[1], -kStaticDb[1] * amt); }
        lastAmt = amt;
    }
    const float maxDyn = kDynMaxDb * amt;
    const int monitor = juce::jlimit (0, 2, (int) pMonitor->load());
    if (monitor != lastMonitor)
    {
        for (auto& c : ch)
        {
            if (monitor == 1)
            {
                c.m1.set (sp::Bq::HP, fs, 400.0, 0.5412); c.m2.set (sp::Bq::HP, fs, 400.0, 1.3066); c.m3.set (sp::Bq::Peak, fs, 2800.0, 1.2, 5.0);
                c.m4.set (sp::Bq::LP, fs, 9000.0, 0.5412); c.m5.set (sp::Bq::LP, fs, 9000.0, 1.3066);
            }
            else if (monitor == 2)
            {
                c.m1.set (sp::Bq::HP, fs, 180.0, 0.5412); c.m2.set (sp::Bq::HP, fs, 180.0, 1.3066); c.m3.set (sp::Bq::Peak, fs, 2200.0, 1.2, 3.5);
                c.m4.set (sp::Bq::LP, fs, 12000.0, 0.7071); c.m5 = {};
            }
            for (auto* f : { &c.m1, &c.m2, &c.m3, &c.m4, &c.m5 }) f->reset();
        }
        lastMonitor = monitor;
    }

    float* L = buffer.getWritePointer (0);
    float* R = numCh > 1 ? buffer.getWritePointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        // 1. mono guard
        if (R != nullptr)
        {
            cLR += corrCoef * (L[i] * R[i] - cLR); cLL += corrCoef * (L[i] * L[i] - cLL); cRR += corrCoef * (R[i] * R[i] - cRR);
            const float corr = cLR / std::sqrt (cLL * cRR + 1e-12f);
            // correlazione sotto zero: il side scende fino a 12 dB (safe width 100 %, correlazione -1)
            sideTargetDb = corr < 0.0f ? -12.0f * safe * juce::jmin (1.0f, -corr) : 0.0f;
            const float target = juce::Decibels::decibelsToGain (sideTargetDb);
            sGain = target + sCoef * (sGain - target);
            const float m = 0.5f * (L[i] + R[i]);
            const float s = sideHp.process (0.5f * (L[i] - R[i])) * sGain;
            L[i] = m + s; R[i] = m - s;
        }
        // 3. de-harsh dinamico: 4 bande strette; una banda scende solo quando sporge sulla media delle altre
        //    (una risonanza), non quando tutto il segnale è forte
        if (amt > 0.0f)
        {
            const float scIn = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            for (int b = 0; b < kDyn; ++b)
            {
                const float v = scBand[(size_t) b].process (scIn), p = v * v;
                auto& e = bandEnv[(size_t) b];
                e = p > e ? eAtk * (e - p) + p : eRel * (e - p) + p;
            }
            if ((counter++ & 15) == 0)
            {
                float db[kDyn], sum = 0.0f;
                for (int b = 0; b < kDyn; ++b) { db[b] = 10.0f * std::log10 (bandEnv[(size_t) b] + 1.0e-18f); sum += db[b]; }
                float worst = 0.0f;
                for (int b = 0; b < kDyn; ++b)
                {
                    const float others = (sum - db[b]) / (float) (kDyn - 1);
                    const float over = db[b] - others - 4.0f;   // 4 dB sopra le vicine = risonanza
                    const float cut = db[b] > -60.0f && over > 0.0f ? juce::jmin (maxDyn, 0.5f * over) : 0.0f;
                    bandGTarget[(size_t) b] = sp::dbToGain (-cut);
                    dynCutDb[(size_t) b].store (cut, std::memory_order_relaxed);
                    worst = juce::jmax (worst, cut);
                }
                curCutDb = worst;
            }
        }
        else if (curCutDb > 0.0f || bandG[0] < 1.0f || bandG[1] < 1.0f || bandG[2] < 1.0f || bandG[3] < 1.0f)
        {
            curCutDb = 0.0f;
            for (int b = 0; b < kDyn; ++b) { bandGTarget[(size_t) b] = 1.0f; dynCutDb[(size_t) b].store (0.0f, std::memory_order_relaxed); }
        }
        for (int b = 0; b < kDyn; ++b) bandG[(size_t) b] += 0.02f * (bandGTarget[(size_t) b] - bandG[(size_t) b]);   // ~1 ms: niente click
        for (int c = 0; c < numCh; ++c)
        {
            auto& st = ch[(size_t) c];
            float x = c == 0 ? L[i] : R[i];
            // 2. phantom thump
            if (thumpG > 0.0f)
            {
                const float xb = st.bassLp.process (st.bassHp.process (x));
                const float a = std::abs (xb);
                st.dc += dcCoef * (a - st.dc);
                const float harm = 0.65f * (a - st.dc) + 0.35f * sp::fastTanh (2.5f * xb);
                x += thumpG * st.harmLp.process (st.harmHp.process (harm));
            }
            if (amt > 0.0f || bandG[0] < 0.9999f || bandG[1] < 0.9999f || bandG[2] < 0.9999f || bandG[3] < 0.9999f)
            {
                x = st.stat2.process (st.stat1.process (x));   // 3a. EQ fisso
                for (int b = 0; b < kDyn; ++b)                   // 3b. tagli stretti, solo dove serve
                {
                    auto& f = st.dyn[(size_t) b];
                    f.tick (x);
                    x += (bandG[(size_t) b] - 1.0f) * f.k * f.bp;
                }
            }
            x = st.codec.process (x);       // 4a. anti-smearing AAC
            (c == 0 ? L : R)[i] = x;
        }
    }
    // riposo: senza segnale correlazione e tagli tornano a zero
    const bool live = cLL + cRR > 1e-10f;
    hasSignal.store (live);
    harshCutDb.store (live ? curCutDb : 0.0f);
    if (R != nullptr) correlation.store (live ? cLR / std::sqrt (cLL * cRR + 1e-12f) : 0.0f);
    sideCutDb.store (live ? -juce::Decibels::gainToDecibels (sGain) : 0.0f);

    // 4b. soft clipper in oversampling 4x: i picchi tra un campione e l'altro restano sotto -1 dBTP
    {
        juce::dsp::AudioBlock<float> block (buffer);
        auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);
        auto up = os->processSamplesUp (sub);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            float* d = up.getChannelPointer (c);
            for (size_t k = 0; k < up.getNumSamples(); ++k) d[k] = kCeiling * sp::fastTanh (d[k] / kCeiling);
        }
        os->processSamplesDown (sub);
    }

    // 4c. limiter true peak: il rilevatore guarda i campioni interpolati 4x; il guadagno scende con una
    //     rampa lineare lungo il lookahead e arriva al valore richiesto esattamente sul picco
    {
        for (int c = 0; c < numCh; ++c) detBuf.copyFrom (c, 0, buffer, c, 0, n);
        juce::dsp::AudioBlock<float> db (detBuf);
        auto up = tpDetect->processSamplesUp (db.getSubsetChannelBlock (0, (size_t) numCh).getSubBlock (0, (size_t) n));
        const int W = kLook - detLag;
        for (int i = 0; i < n; ++i)
        {
            float pk = 0.0f;
            for (size_t c = 0; c < up.getNumChannels(); ++c)
                for (int k = 0; k < 4; ++k) pk = juce::jmax (pk, std::abs (up.getSample ((int) c, 4 * i + k)));
            gReq[(size_t) lookPos] = pk > kLimCeiling ? kLimCeiling / pk : 1.0f;
            float ramp = 1.0f;
            // eventi da 8 campioni prima (margine sull'allineamento del rilevatore) fino a W nel futuro
            for (int k = -8; k <= W; ++k)
            {
                const float g = gReq[(size_t) ((lookPos - W + k) & (kRing - 1))];
                if (g < 1.0f) ramp = juce::jmin (ramp, g + (1.0f - g) * (float) juce::jmax (0, k) / (float) W);   // k = campioni al picco
            }
            limG = juce::jmin (ramp, limG + (1.0f - limG) * limRel);
            for (int c = 0; c < numCh; ++c)
            {
                auto& lb = look[(size_t) c];
                float* d = buffer.getWritePointer (c);
                lb[(size_t) lookPos] = d[i];
                d[i] = lb[(size_t) ((lookPos - (kLook - detLag) - detLag) & (kRing - 1))] * limG;
            }
            lookPos = (lookPos + 1) & (kRing - 1);
        }
    }

    // 5. monitor (solo ascolto)
    if (monitor != 0)
        for (int i = 0; i < n; ++i)
        {
            if (monitor == 1 && R != nullptr) { const float m = 0.5f * (L[i] + R[i]); L[i] = R[i] = m; }   // telefono: mono
            for (int c = 0; c < numCh; ++c)
            {
                auto& st = ch[(size_t) c];
                float x = (c == 0 ? L : R)[i];
                x = st.m3.process (st.m2.process (st.m1.process (x)));
                x = st.m4.process (x);
                if (monitor == 1) x = st.m5.process (x);
                (c == 0 ? L : R)[i] = x;
            }
        }
    if (spectrumActive.load (std::memory_order_relaxed)) spectrumTap.push (buffer.getArrayOfReadPointers(), numCh, n);
    for (auto& c : ch) { for (auto* f : { &c.bassHp, &c.bassLp, &c.harmHp, &c.harmLp, &c.stat1, &c.stat2, &c.codec, &c.m1, &c.m2, &c.m3, &c.m4, &c.m5 }) f->flush(); for (auto& d : c.dyn) d.flush(); }
    sideHp.flush(); for (auto& b : scBand) b.flush();
}

void BAdaptProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BAdaptProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BAdaptProcessor::createEditor() { return new BAdaptEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BAdaptProcessor(); }
