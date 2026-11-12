#include "PluginProcessor.h"
#include "PluginEditor.h"

BTightProcessor::BTightProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "BTight", createLayout())
{
    pTuning = apvts.getRawParameterValue (BTightIDs::tuning);
    pClamp  = apvts.getRawParameterValue (BTightIDs::clamp);
    pAuto   = apvts.getRawParameterValue (BTightIDs::autoMu);
    pBite   = apvts.getRawParameterValue (BTightIDs::bite);
    pPush   = apvts.getRawParameterValue (BTightIDs::push);
    pGain   = apvts.getRawParameterValue (BTightIDs::gain);
    pAutoGain = apvts.getRawParameterValue (BTightIDs::autoGain);
}

juce::AudioProcessorValueTreeState::ParameterLayout BTightProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BTightIDs::tuning, 1 }, "Tuning", StringArray { "Off", "Standard", "Drop", "Baritone" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BTightIDs::clamp, 1 }, "Clamp", NormalisableRange<float> (-48.0f, 0.0f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BTightIDs::autoMu, 1 }, "Clamp auto makeup", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BTightIDs::bite, 1 }, "Bite", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BTightIDs::push, 1 }, "Push", NormalisableRange<float> (0.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BTightIDs::gain, 1 }, "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BTightIDs::autoGain, 1 }, "Autogain", false));
    return l;
}

bool BTightProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BTightProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& c : ch)
    {
        // Linkwitz-Riley 4° ordine = due Butterworth 2° ordine in cascata (basse e alte sommate: piatto in ampiezza)
        c.lp1.set (sp::Bq::LP, fs, 220.0, 0.7071); c.lp2 = c.lp1;
        c.hp1.set (sp::Bq::HP, fs, 220.0, 0.7071); c.hp2 = c.hp1;
        c.hp.reset(); c.lp1.reset(); c.lp2.reset(); c.hp1.reset(); c.hp2.reset(); c.focus.reset();
    }
    clampAtk = sp::coefMs (3.0, fs);  clampRel = sp::coefMs (65.0, fs);
    fAtk = sp::coefMs (0.5, fs);  fRel = sp::coefMs (15.0, fs);
    sAtk = sp::coefMs (15.0, fs); sRel = sp::coefMs (60.0, fs);
    gainSmooth = sp::coefMs (1.0, fs);
    autoCoef = 1.0f - sp::coefMs (1000.0, fs);
    tUp = 1.0f - sp::coefMs (0.2, fs); tDown = 1.0f - sp::coefMs (8.0, fs);
    pickHp1 = {}; pickHp1.set (sp::Bq::HP, fs, 1000.0, 0.7071); pickHp2 = pickHp1;
    avgGrDb = 0; tSm = 0;
    envLow = eFast = eSlow = 0; lowGain = 1;
    lastTuning = -1; lastPush = -100;
    acc = {}; accN = 0;
    outG = juce::Decibels::decibelsToGain (pGain->load()); autoG = 1.0f; eIn = eOut = 0;
}

// 5. uscita: guadagno manuale o autogain (RMS di uscita = RMS d'ingresso), a rampa lineare sul blocco
void BTightProcessor::outputStage (juce::AudioBuffer<float>& buffer, int numCh, double inPow) noexcept
{
    const int n = buffer.getNumSamples();
    const bool autoOn = pAutoGain->load() > 0.5f;
    float target = juce::Decibels::decibelsToGain (pGain->load());
    if (autoOn)
    {
        double outPow = 0;
        for (int c = 0; c < numCh; ++c) { const float r = buffer.getRMSLevel (c, 0, n); outPow += (double) r * r; }
        if (inPow > 1.0e-6 * numCh)   // solo mentre suoni (sopra -60 dBFS)
        {
            const double a = std::exp (-(double) n / (1.0 * fs));
            eIn = a * eIn + (1.0 - a) * inPow; eOut = a * eOut + (1.0 - a) * outPow;
            if (eOut > 1.0e-12) autoG = (float) juce::jlimit (0.063, 15.85, std::sqrt (eIn / eOut));   // -24...+24 dB
        }
        target = autoG;
    }
    if (std::abs (target - outG) < 1.0e-6f && std::abs (outG - 1.0f) < 1.0e-6f) { appliedGainDb.store (0.0f); return; }   // 0 dB: identico
    const float next = outG + (target - outG) * juce::jmin (1.0f, (float) n / (0.03f * (float) fs));   // ~30 ms
    for (int c = 0; c < numCh; ++c) buffer.applyGainRamp (c, 0, n, outG, next);
    outG = next;
    appliedGainDb.store (juce::Decibels::gainToDecibels (outG));
}

void BTightProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numCh = juce::jmin (2, buffer.getNumChannels(), getTotalNumInputChannels());
    const int n = buffer.getNumSamples();

    const int tuning = juce::jlimit (0, 3, (int) pTuning->load());
    const float push = pPush->load();
    if (tuning != lastTuning)
    {
        for (auto& c : ch)
        {
            if (tuning > 0) { c.hp.set (sp::Bq::HP, fs, kTuningHz[tuning], 0.7071); c.hp.reset(); }
            c.lp1.set (sp::Bq::LP, fs, kSplitHz[tuning], 0.7071); c.lp2.b0 = c.lp1.b0; c.lp2.b1 = c.lp1.b1; c.lp2.b2 = c.lp1.b2; c.lp2.a1 = c.lp1.a1; c.lp2.a2 = c.lp1.a2;
            c.hp1.set (sp::Bq::HP, fs, kSplitHz[tuning], 0.7071); c.hp2.b0 = c.hp1.b0; c.hp2.b1 = c.hp1.b1; c.hp2.b2 = c.hp1.b2; c.hp2.a1 = c.hp1.a1; c.hp2.a2 = c.hp1.a2;
        }
        lastTuning = tuning;
    }
    if (std::abs (push - lastPush) > 1e-4f) { for (auto& c : ch) c.focus.set (sp::Bq::Peak, fs, 1400.0, 0.9, push); lastPush = push; }

    double inPow = 0;   // RMS d'ingresso del blocco (per l'autogain)
    for (int c = 0; c < numCh; ++c) { const float r = buffer.getRMSLevel (c, 0, n); inPow += (double) r * r; }
    const float thr = pClamp->load();
    // tutto a zero e tuning spento: nessuna elaborazione, l'uscita è l'ingresso (nemmeno il crossover)
    if (tuning == 0 && thr >= -0.01f && std::abs (pBite->load()) < 0.5f && push < 0.01f)
    {
        for (auto& c : ch) { c.lp1.reset(); c.lp2.reset(); c.hp1.reset(); c.hp2.reset(); c.focus.reset(); }
        envLow = eFast = eSlow = 0; lowGain = 1;
        for (int i = 0; i < n; ++i)
        {
            float pk = 0; for (int c = 0; c < numCh; ++c) pk = juce::jmax (pk, std::abs (buffer.getSample (c, i)));
            acc.peak = juce::jmax (acc.peak, pk);
            if (++accN >= 256) { const int p = (historyPos.load() + 1) & (kHistory - 1); history[(size_t) p] = acc; historyPos.store (p); acc = {}; accN = 0; }
        }
        outputStage (buffer, numCh, inPow);
        return;
    }
    const bool autoOn = pAuto->load() > 0.5f;
    const float bite = pBite->load() / 100.0f;
    const float trim = juce::Decibels::decibelsToGain (-0.5f * push);

    for (int i = 0; i < n; ++i)
    {
        float lo[2] = {}, hi[2] = {};
        float pLow = 0, sc = 0;
        for (int c = 0; c < numCh; ++c)
        {
            auto& s = ch[(size_t) c];
            const float x = tuning > 0 ? s.hp.process (buffer.getSample (c, i)) : buffer.getSample (c, i);
            lo[c] = s.lp2.process (s.lp1.process (x));
            hi[c] = s.hp2.process (s.hp1.process (x));
            pLow = juce::jmax (pLow, lo[c] * lo[c]);
            sc += x;
        }
        // rilevatore della plettrata: il clic sopra 1 kHz (indipendente dal tuning e dalle fondamentali)
        const float pHigh = std::abs (pickHp2.process (pickHp1.process (sc / (float) numCh)));

        // 2. chug clamp: RMS veloce delle basse (collegato tra i canali)
        envLow = pLow > envLow ? clampAtk * (envLow - pLow) + pLow : clampRel * (envLow - pLow) + pLow;
        const float envDb = 3.0103f * std::log2 (envLow + 1e-18f);   // potenza -> dB
        const float grDb = envDb > thr ? -juce::jmin (12.0f, (envDb - thr) * 0.75f) : 0.0f;
        // auto: il make-up insegue la riduzione media (solo mentre le basse suonano)
        if (envDb > -60.0f) avgGrDb += autoCoef * (grDb - avgGrDb);
        const float makeupDb = autoOn ? juce::jlimit (0.0f, 12.0f, -avgGrDb) : 0.0f;
        const float gTarget = sp::dbToGain (grDb + makeupDb);
        lowGain = gTarget + gainSmooth * (lowGain - gTarget);

        // 3. pick attack: inviluppi di picco veloce e lento sulle alte
        eFast = pHigh > eFast ? fAtk * (eFast - pHigh) + pHigh : fRel * (eFast - pHigh) + pHigh;
        eSlow = pHigh > eSlow ? sAtk * (eSlow - pHigh) + pHigh : sRel * (eSlow - pHigh) + pHigh;
        const float diff = (eFast - eSlow) / (eSlow + 1e-6f);
        const float tRaw = diff > 0.0f ? bite * juce::jmin (9.0f, diff * 12.0f) : 0.0f;
        tSm += (std::abs (tRaw) > std::abs (tSm) ? tUp : tDown) * (tRaw - tSm);   // guadagno levigato: niente click
        const float tDb = tSm;
        const float hiGain = sp::dbToGain (tDb);

        float peak = 0;
        for (int c = 0; c < numCh; ++c)
        {
            float y = lo[c] * lowGain + hi[c] * hiGain;
            y = ch[(size_t) c].focus.process (y) * trim;          // 4. focus tilt
            buffer.setSample (c, i, y);
            peak = juce::jmax (peak, std::abs (y));
        }

        acc.peak = juce::jmax (acc.peak, peak);
        acc.clampDb = juce::jmax (acc.clampDb, -grDb);
        acc.attackDb = std::abs (tDb) > std::abs (acc.attackDb) ? tDb : acc.attackDb;
        if (++accN >= 256)
        {
            const int p = (historyPos.load (std::memory_order_relaxed) + 1) & (kHistory - 1);
            history[(size_t) p] = acc;
            historyPos.store (p, std::memory_order_release);
            acc = {}; accN = 0;
        }
    }
    pickHp1.flush(); pickHp2.flush();
    for (auto& c : ch) { c.hp.flush(); c.lp1.flush(); c.lp2.flush(); c.hp1.flush(); c.hp2.flush(); c.focus.flush(); }
    outputStage (buffer, numCh, inPow);
}

void BTightProcessor::applyTuningPreset (int tuning)
{
    const auto& pr = kPresets[juce::jlimit (0, 3, tuning)];
    for (auto [id, v] : { std::pair<const char*, float> { BTightIDs::clamp, pr.clamp }, { BTightIDs::bite, pr.bite }, { BTightIDs::push, pr.push } })
    {
        auto* p = apvts.getParameter (id);
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture();
    }
}

void BTightProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BTightProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BTightProcessor::createEditor() { return new BTightEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BTightProcessor(); }
