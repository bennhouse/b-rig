#include "PluginProcessor.h"
#include "PluginEditor.h"

BFreshstringsProcessor::BFreshstringsProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "BFreshstrings", createLayout())
{
    pPick = apvts.getRawParameterValue (BFreshIDs::pick);
    pGauge = apvts.getRawParameterValue (BFreshIDs::gauge);
    pPickup = apvts.getRawParameterValue (BFreshIDs::pickup);
    pFresh = apvts.getRawParameterValue (BFreshIDs::freshness);
    pAttack = apvts.getRawParameterValue (BFreshIDs::attack);
    pTension = apvts.getRawParameterValue (BFreshIDs::tension);
    pOut = apvts.getRawParameterValue (BFreshIDs::output);
}

juce::AudioProcessorValueTreeState::ParameterLayout BFreshstringsProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BFreshIDs::pick, 1 }, "Pick", StringArray { "Nylon", "Tortex", "Carbon", "Metal" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BFreshIDs::gauge, 1 }, "Gauge", StringArray { ".009", ".010", ".011", "Heavy" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BFreshIDs::pickup, 1 }, "Pickup", StringArray { "Bridge", "Both", "Neck" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BFreshIDs::freshness, 1 }, "Freshness", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BFreshIDs::attack, 1 }, "Attack", NormalisableRange<float> (-6.0f, 6.0f, 0.1f), 2.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BFreshIDs::tension, 1 }, "Tension", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BFreshIDs::output, 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

bool BFreshstringsProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == juce::AudioChannelSet::mono() && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

void BFreshstringsProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    detHp.set (fs, 1500.0); detHp.reset();
    hf22.set (fs, 2200.0); hf22.reset();
    exHp.set (fs, 3000.0); exHp.reset();
    shelf32.set (fs, 3200.0); shelf32.reset();
    lowDet.set (fs, 150.0); lowDet.reset();
    lowShelf.reset();
    pickBand.reset(); lowBand.set (fs, 180.0); lowBand.reset();
    peak.reset(); roll.reset(); gaugeHigh.f.reset(); pickupLow.f.reset(); curLowDb = kPickups[juce::jlimit (0, 2, (int) pPickup->load())].lowDb;
    for (auto& a : ap) a.z = 0.0f;
    fAtk = sp::coefMs (0.3, fs); fRel = sp::coefMs (8.0, fs);
    sAtk = sp::coefMs (8.0, fs); sRel = sp::coefMs (80.0, fs);
    trRel = sp::coefMs (60.0, fs);
    lowAtk = sp::coefMs (5.0, fs); lowRel = sp::coefMs (80.0, fs);
    eFast = eSlow = trSm = pickEnv = envLow = 0.0f;
    comb.fill (0.0f); combPos = 0;
    combDelay = (float) (kCombMs * 0.001 * fs);
    if (combDelay > (float) (kComb - 3)) combDelay = (float) (kComb - 3);
    outG = outTarget = sp::dbToGain (pOut->load());
    updateControls (true);
}

// ogni 32 campioni: coefficienti dei filtri e valori del pickup che inseguono quelli scelti (~30 ms)
void BFreshstringsProcessor::updateControls (bool snap) noexcept
{
    const int pk = juce::jlimit (0, 3, (int) pPick->load());
    const int gg = juce::jlimit (0, 3, (int) pGauge->load());
    const int pu = juce::jlimit (0, 2, (int) pPickup->load());
    const float attack = pAttack->load(), tension = pTension->load() / 100.0f;
    fresh = pFresh->load() / 100.0f;
    outTarget = sp::dbToGain (pOut->load());

    // 2. pick: banda, spinta e durata dell'attacco
    static constexpr float kHz[4] = { 4500.0f, 1800.0f, 3400.0f, 5200.0f }, kQ[4] = { 0.7071f, 1.0f, 1.2f, 2.5f };
    // plettro: più è duro, più breve è il contatto con la corda e più alte arrivano sull'attacco. Nylon (flessibile)
    // rilascia in ~4 ms e smussa gli spigoli; tortex (acetale) ~2 ms, medio-alte; carbon (rigido) ~1 ms, attacco
    // secco a 3,4 kHz; metal (acciaio) il rilascio più breve (~0,7 ms) con lo "sfrigolio" risonante a 5,2 kHz
    static constexpr float kOffDb[4] = { -6.0f, 0.0f, 1.0f, 6.0f }, kMs[4] = { 4.0f, 2.0f, 1.0f, 0.7f };
    pickIsHp = pk == 0;
    pickBand.set (fs, kHz[pk], kQ[pk]);
    pickBandGain = sp::dbToGain (attack + kOffDb[pk]) - 1.0f;
    pickLowGain = pk == 2 ? sp::dbToGain (-4.0f) - 1.0f : 0.0f;
    pickDecay = sp::coefMs (kMs[pk], fs);

    // 3. gauge: passa-tutto (più tensione = più dispersione) e scaffale a 110 Hz
    const float apHz = juce::jlimit (800.0f, 12000.0f, kGaugeApHz[gg] * std::exp2 ((0.5f - tension) * 2.0f));
    for (auto& a : ap) a.set (fs, apHz);
    float shelfDb = kGaugeShelfDb[gg] * tension * 2.0f;
    if (shelfDb < 0.0f)   // le corde sottili si asciugano solo quando le basse suonano forte
        shelfDb *= juce::jlimit (0.0f, 1.0f, (sp::gainToDb (envLow) + 40.0f) / 20.0f);
    // scaffale basso esatto su stato-variabile (Cytomic): g = tan(pi f / fs) / sqrt(A), y = x + k(A-1) bp + (A^2-1) lp
    const float A = std::exp2 (shelfDb / 40.0f * 3.32192809f);
    lowShelf.g = (float) std::tan (juce::MathConstants<double>::pi * 110.0 / fs) / std::sqrt (A);
    lowShelf.k = 1.41421356f;
    lowShelf.a1 = 1.0f / (1.0f + lowShelf.g * (lowShelf.g + lowShelf.k)); lowShelf.a2 = lowShelf.g * lowShelf.a1; lowShelf.a3 = lowShelf.g * lowShelf.a2;
    shelfM1 = lowShelf.k * (A - 1.0f); shelfM2 = A * A - 1.0f;

    gaugeHigh.set (fs, 4000.0, kGaugeHighDb[gg] * tension * 2.0f, false);
    // 4. pickup
    const auto& t = kPickups[pu];
    const float a = snap ? 1.0f : 1.0f - std::exp (-32.0f / (0.03f * (float) fs));
    curHz += a * (t.hz - curHz); curQ += a * (t.q - curQ); curDb += a * (t.db - curDb);
    curComb += a * (t.combGain - curComb); curRoll += a * (t.rollHz - curRoll); curLowDb += a * (t.lowDb - curLowDb);
    pickupLow.set (fs, 250.0, curLowDb, true);
    peak.set (fs, curHz, curQ);
    peakG = sp::dbToGain (curDb) - 1.0f;
    roll.set (fs, curRoll, 0.7071);
}

void BFreshstringsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0) return;
    float* io = buffer.getWritePointer (0);
    float lvl = 0.0f, frMax = 0.0f, pkMax = 0.0f;
    const float shelfBoost = sp::dbToGain (6.0f) - 1.0f;

    for (int i = 0; i < n; ++i)
    {
        if (ctl-- <= 0) { updateControls (false); ctl = 31; }
        const float x = io[i];

        // rilevatore della plettrata: inviluppo veloce contro lento sopra 1,5 kHz
        const float d = std::abs (detHp.processHP (x));
        eFast = d > eFast ? fAtk * (eFast - d) + d : fRel * (eFast - d) + d;
        eSlow = d > eSlow ? sAtk * (eSlow - d) + d : sRel * (eSlow - d) + d;
        const float tr = eFast > 1.5e-3f ? juce::jlimit (0.0f, 1.0f, 0.5f * (eFast - eSlow) / (eSlow + 1.0e-5f)) : 0.0f;
        trSm = tr > trSm ? tr : trRel * trSm;
        pickEnv = juce::jmax (tr, pickDecay * pickEnv);
        const float lx = std::abs (lowDet.processLP (x));
        envLow = lx > envLow ? lowAtk * (envLow - lx) + lx : lowRel * (envLow - lx) + lx;

        float y = x;
        // 1. fresh strings: armoniche sulla banda alta e scaffale dinamico a 3,2 kHz
        const float xhf = hf22.processHP (x);
        const float u = 6.0f * xhf;
        const float harm = (sp::fastTanh (u) - u) + 0.35f * u * std::abs (u);
        y += fresh * (0.3f + 0.7f * trSm) * 0.5f * exHp.processHP (harm);
        y += fresh * trSm * shelfBoost * shelf32.processHP (x);

        // 2. pick: la banda del plettro nei primi millisecondi
        pickBand.tick (x);
        const float band = pickIsHp ? pickBand.hp : pickBand.k * pickBand.bp;
        y += pickEnv * pickBandGain * band;
        if (pickLowGain != 0.0f) y += pickEnv * pickLowGain * lowBand.processLP (x);

        // 3. gauge: dispersione e peso delle basse
        for (auto& a : ap) y = a.process (y);
        lowShelf.tick (y);
        y += shelfM1 * lowShelf.bp + shelfM2 * lowShelf.lp;
        y = gaugeHigh.process (y);

        // 4. pickup: risonanza, pettine (posizione) e roll-off
        peak.tick (y);
        y += peakG * peak.k * peak.bp;
        comb[(size_t) combPos] = y;
        {
            const float rp = (float) combPos - combDelay + (float) kComb;
            const int i0 = (int) rp; const float f = rp - (float) i0;
            const float dly = comb[(size_t) (i0 % kComb)] + f * (comb[(size_t) ((i0 + 1) % kComb)] - comb[(size_t) (i0 % kComb)]);
            y = (y + curComb * dly) / (1.0f + curComb);
        }
        combPos = (combPos + 1) % kComb;
        y = roll.processLP (pickupLow.process (y));

        outG += 0.002f * (outTarget - outG);
        io[i] = y * outG;
        lvl = juce::jmax (lvl, std::abs (x));
        frMax = juce::jmax (frMax, fresh * trSm);
        pkMax = juce::jmax (pkMax, pickEnv);
    }
    for (auto* f : { &detHp, &hf22, &exHp, &shelf32, &pickBand, &lowBand, &lowShelf, &lowDet, &peak, &roll, &gaugeHigh.f, &pickupLow.f }) f->flush();
    levelNow.store (juce::jmax (lvl, levelNow.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    freshNow.store (juce::jmax (frMax, freshNow.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
    pickNow.store (juce::jmax (pkMax, pickNow.load (std::memory_order_relaxed) * 0.8f), std::memory_order_relaxed);
    for (int c = 1; c < buffer.getNumChannels(); ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);
}

void BFreshstringsProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BFreshstringsProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BFreshstringsProcessor::createEditor() { return new BFreshstringsEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BFreshstringsProcessor(); }
