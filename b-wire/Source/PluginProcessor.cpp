#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace { const char* ids[BWireProcessor::kNumP] = { BWireIDs::tracking, BWireIDs::ring, BWireIDs::fuzz, BWireIDs::sub1, BWireIDs::sub2, BWireIDs::glitch,
                                                     BWireIDs::filter, BWireIDs::level, BWireIDs::mix, BWireIDs::levelAuto }; }

BWireProcessor::BWireProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "BWire", createLayout())
{
    for (int i = 0; i < kNumP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

juce::AudioProcessorValueTreeState::ParameterLayout BWireProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    NormalisableRange<float> tr (2.0f, 45.0f, 0.1f); tr.setSkewForCentre (10.0f);
    f (BWireIDs::tracking, "Tracking", tr, 5.0f, "ms");
    f (BWireIDs::fuzz, "Fuzz", { 0.0f, 100.0f, 1.0f }, 60.0f, "%");
    f (BWireIDs::sub1, "Sub 1", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BWireIDs::sub2, "Sub 2", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BWireIDs::glitch, "Glitch", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    NormalisableRange<float> fr (400.0f, 12000.0f, 1.0f); fr.setSkewForCentre (2200.0f);
    f (BWireIDs::ring, "Ring", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BWireIDs::filter, "Filter", fr, 3000.0f, "Hz");
    f (BWireIDs::mix, "Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, "%");
    f (BWireIDs::level, "Level", { -24.0f, 12.0f, 0.1f }, -6.0f, "dB");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BWireIDs::levelAuto, 1 }, "Level auto", false));
    return l;
}

bool BWireProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());   // niente mono > stereo
}

void BWireProcessor::prepareToPlay (double rate, int)
{
    fs = rate;
    detHp = {}; detHp.set (sp::Bq::HP, fs, 65.0, 0.7071);
    detLp = {}; detLp.set (sp::Bq::LP, fs, 900.0, 0.7071);
    eAtk = sp::coefMs (2.0, fs); eRel = sp::coefMs (25.0, fs);
    gAtk = 1.0f - sp::coefMs (3.0, fs); gRel = 1.0f - sp::coefMs (60.0, fs);
    env = prevX = sinceRise = period = gateEnv = 0; cmp = -1;
    phase = 0; sub1 = sub2 = 1; sub2Next = false;
    for (auto& v : svfIc1) v = 0; for (auto& v : svfIc2) v = 0; lastCut = -1;
    mixSm.reset (fs, 0.02); mixSm.setCurrentAndTargetValue (p[8]->load() / 100.0f);
    levelSm.reset (fs, 0.02); levelSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p[7]->load()));
    aRms = sp::coefMs (400.0, fs); aGain = 1.0f - sp::coefMs (250.0, fs);
    eIn = eOut = 0; autoG = 1;
}

void BWireProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels(), n = buffer.getNumSamples();
    const float trackMs = p[0]->load(), wR = p[1]->load() / 100.0f;
    const float wF = p[2]->load() / 100.0f, w1 = p[3]->load() / 100.0f, w2 = p[4]->load() / 100.0f, wG = p[5]->load() / 100.0f;
    const float cut = p[6]->load();
    const bool autoLevel = p[9]->load() > 0.5f;
    mixSm.setTargetValue (p[8]->load() / 100.0f);
    levelSm.setTargetValue (juce::Decibels::decibelsToGain (p[7]->load()));
    constexpr float driveG = 7.94f;   // +18 dB fissi nel fuzz (il carattere lo dà la quantità di fuzz)
    const float trackA = 1.0f / (trackMs * 0.001f * (float) fs);
    if (std::abs (cut - lastCut) > 0.5f)
    {
        const float g = (float) std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) cut, 0.45 * fs) / fs);
        svfK = 1.0f / 1.4f;
        svfA1 = 1.0f / (1.0f + g * (g + svfK)); svfA2 = g * svfA1; svfA3 = g * svfA2; lastCut = cut;
    }
    const float gateThr = juce::Decibels::decibelsToGain (-54.0f);
    float* L = buffer.getWritePointer (0);
    float* R = numIn > 1 ? buffer.getWritePointer (1) : nullptr;
    juce::ignoreUnused (numOut);
    float peakEnv = 0;

    for (int i = 0; i < n; ++i)
    {
        const float x = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        // 1. rilevamento: banda 65-900 Hz, RMS veloce, Schmitt a soglie adattive
        const float xf = detLp.process (detHp.process (x));
        const float pw = xf * xf;
        env = pw > env ? eAtk * (env - pw) + pw : eRel * (env - pw) + pw;
        const float rms = std::sqrt (env);
        const bool gateOpen = rms > gateThr;
        const float thr = 0.18f * rms;
        sinceRise += 1.0f;
        if (cmp < 0 && xf > thr && gateOpen)
        {
            cmp = 1;
            const float frac = (xf - thr) / juce::jmax (1e-9f, xf - prevX);   // frazione di campione dal superamento
            const float measured = sinceRise - frac;
            sinceRise = frac;
            if (measured > fs / 1200.0 && measured < fs / 40.0)
            {
                if (period <= 0) period = measured;
                const float a = 1.0f - std::exp2 (-1.442695f * measured * trackA);
                period += a * (measured - period);
            }
        }
        else if (cmp > 0 && xf < -thr) cmp = -1;
        prevX = xf;
        gateEnv += ((gateOpen ? juce::jmin (1.0f, rms * 12.0f) : 0.0f) - gateEnv) * (gateOpen ? gAtk : gRel);
        if (! gateOpen && gateEnv < 1.0e-4f) gateEnv = 0.0f;   // chiuso del tutto: silenzio vero

        // 2. oscillatore PolyBLEP e flip-flop
        float sq = 0, s1 = 0, s2 = 0;
        if (period > 0)
        {
            const float dt = juce::jlimit (1e-5f, 0.45f, 1.0f / period);
            phase += dt;
            if (phase >= 1.0f)
            {
                phase -= 1.0f;
                sub1 = -sub1;
                if (sub2Next) sub2 = -sub2;
                sub2Next = ! sub2Next;
            }
            sq = (phase < 0.5f ? 1.0f : -1.0f) + polyBlep (phase, dt) - polyBlep (std::fmod (phase + 0.5f, 1.0f), dt);
            // fronti dei flip-flop sul giro della fase master: dopo il fronte (t < dt) il salto appena fatto,
            // prima del fronte (t > 1 - dt) quello che sta per arrivare
            const float b = polyBlep (phase, dt);
            s1 = sub1 + (phase < dt ? sub1 : -sub1) * b;
            const bool flipsNow = phase < dt ? ! sub2Next : sub2Next;   // il sub -2 cambia una volta su due
            s2 = sub2 + (flipsNow ? (phase < dt ? sub2 : -sub2) * b : 0.0f);
        }
        // 3. fuzz, xor e ring modulator adattivo (portante = seno alla fondamentale agganciata)
        const float fz = sp::fastTanh (driveG * x);
        float ringV = 0.0f;
        if (wR > 0.0f && period > 0)
        {
            const float ph = phase - 0.5f;   // seno parabolico (Bhaskara), continuo e senza tabelle
            const float sn = -4.0f * ph * (1.0f - 2.0f * std::abs (ph)) * 2.0f;
            ringV = 2.0f * x * sn;
        }
        float y = wF * fz + gateEnv * (w1 * 0.5f * s1 + w2 * 0.5f * s2 + wR * ringV) + wG * fz * s1;
        // 4. passa-basso SVF (Q 1,4)
        const float v3 = y - svfIc2[0];
        const float v1 = svfA1 * svfIc1[0] + svfA2 * v3;
        const float v2 = svfIc2[0] + svfA2 * svfIc1[0] + svfA3 * v3;
        svfIc1[0] = 2 * v1 - svfIc1[0]; svfIc2[0] = 2 * v2 - svfIc2[0];
        // 5. mix dry/wet e livello (fisso, oppure auto: RMS dell'uscita pareggiato all'ingresso mentre suoni)
        const float m = mixSm.getNextValue();
        const float wetMix = x + m * (v2 - x);
        if (gateOpen) { eIn = aRms * eIn + (1.0f - aRms) * x * x; eOut = aRms * eOut + (1.0f - aRms) * wetMix * wetMix; }
        const float lv = levelSm.getNextValue();
        if (autoLevel)
        {
            if (gateOpen && eOut > 1.0e-9f)
                autoG += aGain * (juce::jlimit (0.063f, 4.0f, std::sqrt (eIn / eOut)) - autoG);   // -24...+12 dB
            y = wetMix * autoG;
        }
        else y = wetMix * lv;
        L[i] = y;
        if (R != nullptr) R[i] = y;
        peakEnv = juce::jmax (peakEnv, gateEnv);

        const int sp_ = (scopePos.load (std::memory_order_relaxed) + 1) & (kScope - 1);
        scope[(size_t) sp_] = { sq, s1, s2 };
        scopePos.store (sp_, std::memory_order_release);
    }
    pllHz.store (period > 0 && peakEnv > 0.01f ? (float) (fs / period) : 0.0f);
    gateLevel.store (peakEnv);
    autoGainDb.store (autoLevel ? juce::Decibels::gainToDecibels (autoG) : -1000.0f);
    detHp.flush(); detLp.flush();
}

void BWireProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
void BWireProcessor::setStateInformation (const void* d, int s)
{
    if (auto x = getXmlFromBinary (d, s)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
}
juce::AudioProcessorEditor* BWireProcessor::createEditor() { return new BWireEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BWireProcessor(); }
