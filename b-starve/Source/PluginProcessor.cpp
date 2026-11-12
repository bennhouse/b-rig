#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // finestra coseno: 0 -> 1 in 64 campioni (crossfade anti-click)
    inline float xfade (int i, int len) { return i >= len ? 1.0f : 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * (float) i / (float) len); }
    constexpr int kX = 64;
}

BStarveProcessor::BStarveProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BStarve", createLayout())
{
    for (int e = 0; e < 4; ++e)
    {
        pOn[e] = apvts.getRawParameterValue (BStarveIDs::on (e));
        pInt[e] = apvts.getRawParameterValue (BStarveIDs::intensity (e));
        pMix[e] = apvts.getRawParameterValue (BStarveIDs::mix (e));
    }
    pOut = apvts.getRawParameterValue (BStarveIDs::output);
    for (int e = 0; e < 4; ++e) { pVol[e] = apvts.getRawParameterValue (BStarveIDs::volume (e)); lastOn[(size_t) e] = pOn[e]->load(); }
    startTimerHz (20);
}

void BStarveProcessor::timerCallback()
{
    for (int e = 0; e < 4; ++e)
        if (calReady[(size_t) e].exchange (false))
        {
            auto* prm = apvts.getParameter (BStarveIDs::volume (e));
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (calResult[(size_t) e].load()));
            prm->endChangeGesture();
        }
}

juce::AudioProcessorValueTreeState::ParameterLayout BStarveProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const char* names[4] = { "Starve", "Mosh", "Corrupt", "Melt" };
    for (int e = 0; e < 4; ++e)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { BStarveIDs::on (e), 1 }, String (names[e]) + " on", false));   // tutto spento all'apertura
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { BStarveIDs::intensity (e), 1 }, String (names[e]) + " intensity",
                                                      NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, AudioParameterFloatAttributes().withLabel ("%")));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { BStarveIDs::mix (e), 1 }, String (names[e]) + " mix",
                                                      NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { BStarveIDs::volume (e), 1 }, String (names[e]) + " volume",
                                                      NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BStarveIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    return l;
}

bool BStarveProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BStarveProcessor::prepareToPlay (double rate, int)
{
    fs = rate;
    for (auto& c : ch)
    {
        c = {};
        c.buf.assign ((size_t) juce::nextPowerOfTwo ((int) std::ceil (0.25 * fs)), 0.0f);   // >= 250 ms (potenza di 2), allocati qui
        c.sagLp.set (sp::Bq::LP, fs, 200.0, 0.7071);
    }
    onCoef = 1.0f - sp::coefMs (20.0, fs);
    sagA = sp::coefMs (8.0, fs); sagR = sp::coefMs (140.0, fs);
    meltR = sp::coefMs (300.0, fs); peakR = sp::coefMs (2000.0, fs);
    outC = 1.0f - sp::coefMs (50.0, fs);
    for (int e = 0; e < 4; ++e) onG[(size_t) e] = pOn[e]->load() > 0.5f ? 1.0f : 0.0f;
    outG = juce::Decibels::decibelsToGain (pOut->load()); clock = 0; nextTick = 0;
    bufMask = (int) ch[0].buf.size() - 1;
    for (int e = 0; e < 4; ++e) lastOn[(size_t) e] = pOn[e]->load();
    volC = 1.0f - sp::coefMs (30.0, fs);
    for (int e = 0; e < 4; ++e) volG[(size_t) e] = juce::Decibels::decibelsToGain (pVol[e]->load());
}

// 1. tensione che crolla: il bias si sposta con le basse, gate a zona morta, saturazione asimmetrica
float BStarveProcessor::starve (Ch& c, float x, float k)
{
    const float low = std::abs (c.sagLp.process (x));
    c.sag = low > c.sag ? sagA * (c.sag - low) + low : sagR * (c.sag - low) + low;
    const float v = juce::jmin (1.0f, c.sag * 4.0f);
    const float b = k * 0.65f * v, thr = k * 0.04f, drive = 1.0f + 8.0f * k;
    const float xg = std::abs (x) < thr ? 0.0f : x - std::copysign (thr, x);
    return (sp::fastTanh (drive * (xg + b)) - sp::fastTanh (drive * b)) / (1.0f + 0.3f * drive);
}

// 2. stutter: finestre ripetute (anche al contrario) con fade coseno a ogni giro e in entrata/uscita
float BStarveProcessor::mosh (Ch& c, float x, float k, int chIdx)
{
    const int size = (int) c.buf.size();
    c.buf[(size_t) c.w] = x;
    if (chIdx == 0 && clock == 0 && k > 0.0f && ! c.stut && rng.nextFloat() < k)
    {
        c.segLen = juce::jmax (2 * kX, (int) ((0.060 - 0.054 * k) * fs));
        c.segStart = (c.w - c.segLen + size) & bufMask;
        c.idx = 0; c.repeats = 2 + rng.nextInt (5); c.rev = rng.nextFloat() < 0.25f; c.stut = true;
        ch[1].segLen = c.segLen; ch[1].segStart = (ch[1].w - c.segLen + size) & bufMask; ch[1].idx = 0;
        ch[1].repeats = c.repeats; ch[1].rev = c.rev; ch[1].stut = true;
    }
    c.w = (c.w + 1) & bufMask;
    float s = 0;
    if (c.stut)
    {
        const int i = c.rev ? c.segLen - 1 - c.idx : c.idx;
        const float win = juce::jmin (xfade (c.idx, kX), xfade (c.segLen - 1 - c.idx, kX));
        s = c.buf[(size_t) ((c.segStart + i) & bufMask)] * win;
        if (++c.idx >= c.segLen) { c.idx = 0; if (--c.repeats <= 0) c.stut = false; }
    }
    c.moshMix += ((c.stut ? 1.0f : 0.0f) - c.moshMix) * (1.0f / kX);   // ~64 campioni tra suono vivo e congelato
    return x + c.moshMix * (s - x);
}

// 3. pacchetti corrotti: XOR sui bit bassi del campione a 16 bit e buchi ritmici
float BStarveProcessor::corrupt (Ch& c, float x, float k)
{
    const int q = juce::jlimit (-32768, 32767, (int) std::lround (x * 32767.0f));
    if (--c.maskTimer <= 0)
    {
        const int bits = 1 + (int) (k * 10.0f);
        c.maskOld = c.maskNew; c.maskNew = rng.nextInt (1 << bits); c.maskX = 0;
        c.maskTimer = (int) ((0.002 + 0.006 * rng.nextFloat()) * fs);
    }
    const float a = (float) (q ^ c.maskOld) / 32767.0f, b = (float) (q ^ c.maskNew) / 32767.0f;
    float y = a + xfade (c.maskX++, kX) * (b - a);
    // buchi: 3-15 ms, frequenza proporzionale a intensity, rampe di 1 ms
    if (c.dropLeft <= 0 && --c.dropTimer <= 0)
    {
        c.dropLeft = (int) ((0.003 + 0.012 * rng.nextFloat()) * fs);
        c.dropTimer = (int) (fs / juce::jmax (0.5f, 6.0f * k)) + rng.nextInt ((int) (0.05 * fs));
    }
    const float target = c.dropLeft > 0 && k > 0.0f ? 0.0f : 1.0f;
    if (c.dropLeft > 0) --c.dropLeft;
    c.dropG += (target - c.dropG) * (1.0f / (0.001f * (float) fs));
    return y * c.dropG;
}

// 4. scioglimento: sample & hold sempre più lento mentre la nota decade, filtro risonante che ondeggia
float BStarveProcessor::melt (Ch& c, float x, float k)
{
    const float a = std::abs (x);
    c.env = a > c.env ? a : meltR * c.env;
    c.peak = juce::jmax (c.env, peakR * c.peak, 1.0e-4f);
    const float norm = c.env / c.peak;
    const float factor = 1.0f + k * 28.0f * (1.0f - norm);
    c.ph += 1.0f / factor;
    if (c.ph >= 1.0f) { c.ph -= std::floor (c.ph); c.held = x; }
    if ((c.ctl++ & 15) == 0)   // coefficienti del filtro a control rate (ogni 16 campioni): niente tan/sin per campione
    {
        const float fc = juce::jlimit (200.0f, 0.45f * (float) fs, (float) fs / (2.0f * factor) * (1.0f + 0.15f * std::sin (lfo)));
        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) fs), kq = 1.0f / 4.0f;
        c.a1 = 1.0f / (1.0f + g * (g + kq)); c.a2 = g * c.a1; c.a3 = g * c.a2;
    }
    const float v3 = c.held - c.ic2, v1 = c.a1 * c.ic1 + c.a2 * v3, v2 = c.ic2 + c.a2 * c.ic1 + c.a3 * v3;
    c.ic1 = 2 * v1 - c.ic1; c.ic2 = 2 * v2 - c.ic2;
    return juce::jlimit (-1.5f, 1.5f, v2);
}

void BStarveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numCh = juce::jmin (2, getTotalNumInputChannels()), n = buffer.getNumSamples();
    float k[4], m[4], tOn[4];
    float vT[4];
    for (int e = 0; e < 4; ++e)
    {
        k[e] = pInt[e]->load() / 100.0f; m[e] = pMix[e]->load() / 100.0f; tOn[e] = pOn[e]->load() > 0.5f ? 1.0f : 0.0f;
        vT[e] = juce::Decibels::decibelsToGain (pVol[e]->load());
        // prima accensione dall'apertura del plugin: si misura e si calibra il volume (una volta sola)
        if (tOn[e] > 0.5f && lastOn[(size_t) e] < 0.5f && calState[(size_t) e].load() == 0)
        {
            calState[(size_t) e].store (1); calIn[(size_t) e] = calOut[(size_t) e] = 0.0; calCount[(size_t) e] = 0;
        }
        if (tOn[e] < 0.5f && calState[(size_t) e].load() == 1) calState[(size_t) e].store (0);   // spento prima della fine: si rifà
        lastOn[(size_t) e] = tOn[e];
    }
    const int calNeed = (int) (0.4 * fs), calSkip = (int) (0.25 * fs);
    const float calGate = 0.001f;   // -60 dBFS: si misura solo mentre suoni
    const float outT = juce::Decibels::decibelsToGain (pOut->load());
    bool anyOn = false; for (int e = 0; e < 4; ++e) anyOn = anyOn || tOn[e] > 0 || onG[(size_t) e] > 1e-4f;
    if (! anyOn && std::abs (outT - 1.0f) < 1e-6f && std::abs (outG - 1.0f) < 1e-6f) return;   // tutto spento: uscita identica

    const float lfoInc = (float) (juce::MathConstants<double>::twoPi * 0.7 / fs);
    for (int i = 0; i < n; ++i)
    {
        if (--nextTick <= 0) { clock = 0; nextTick = (int) ((0.016 + 0.064 * rng.nextFloat()) * fs); } else clock = 1;
        lfo += lfoInc; if (lfo > juce::MathConstants<float>::twoPi) lfo -= juce::MathConstants<float>::twoPi;
        for (int e = 0; e < 4; ++e)
        {
            onG[(size_t) e] += (tOn[e] - onG[(size_t) e]) * onCoef;
            volG[(size_t) e] += volC * (vT[e] - volG[(size_t) e]);
        }
        float y[2] = {};
        for (int c = 0; c < numCh; ++c)
        {
            auto& s = ch[(size_t) c];
            const float x = buffer.getReadPointer (c)[i];
            float v = x;
            float stage[4];
            for (int e = 0; e < 4; ++e)
            {
                float wet;
                switch (e)
                {
                    case 0:  wet = starve (s, v, k[0]); break;
                    case 1:  wet = mosh (s, v, k[1], c); break;
                    case 2:  wet = corrupt (s, v, k[2]); break;
                    default: wet = melt (s, v, k[3]); break;
                }
                if (c == 0 && calState[(size_t) e].load (std::memory_order_relaxed) == 1 && std::abs (v) > calGate)
                {
                    // i primi 250 ms di suono si lasciano assestare (dissolvenza, sag, inviluppi), poi 400 ms di misura
                    if (++calCount[(size_t) e] > calSkip) { calIn[(size_t) e] += (double) v * v; calOut[(size_t) e] += (double) wet * wet; }
                    if (calCount[(size_t) e] >= calSkip + calNeed)
                    {
                        const double r = calOut[(size_t) e] > 1e-12 ? std::sqrt (calIn[(size_t) e] / calOut[(size_t) e]) : 1.0;
                        const float db = juce::jlimit (-24.0f, 24.0f, (float) (20.0 * std::log10 (r)));
                        calResult[(size_t) e].store (std::round (db * 10.0f) / 10.0f);
                        calReady[(size_t) e].store (true);
                        calState[(size_t) e].store (2);
                    }
                }
                const float g = onG[(size_t) e] * m[e];
                v += g * (wet * volG[(size_t) e] - v);
                stage[e] = v;
            }
            y[c] = v;
            if (c == 0 && ++scopeDecim >= 4)
            {
                scopeDecim = 0;
                const int p = (scopePos.load (std::memory_order_relaxed) + 1) & (kScope - 1);
                for (int e = 0; e < 4; ++e) scope[(size_t) e][(size_t) p] = stage[e];
                scopePos.store (p, std::memory_order_release);
            }
        }
        outG += outC * (outT - outG);
        for (int c = 0; c < numCh; ++c) buffer.getWritePointer (c)[i] = y[c] * outG;
    }
    for (auto& c : ch) c.sagLp.flush();
}

void BStarveProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
void BStarveProcessor::setStateInformation (const void* d, int s)
{
    if (auto x = getXmlFromBinary (d, s)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
    for (int e = 0; e < 4; ++e) lastOn[(size_t) e] = pOn[e]->load();   // un effetto già acceso nel progetto non si ricalibra
}
juce::AudioProcessorEditor* BStarveProcessor::createEditor() { return new BStarveEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BStarveProcessor(); }
