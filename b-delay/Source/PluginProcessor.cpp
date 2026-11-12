#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    enum { P_TYPE, P_TIME, P_SYNC, P_DIV, P_FB, P_LOCUT, P_HICUT, P_MIX, P_OFFSET, P_PP, P_MOD, P_SMEAR, P_DUCK, P_OUT, P_TRAILS, P_BYPASS };
    constexpr const char* kIds[16] = { BDelayIDs::type, BDelayIDs::time, BDelayIDs::sync, BDelayIDs::division, BDelayIDs::feedback, BDelayIDs::locut,
                                       BDelayIDs::hicut, BDelayIDs::mix, BDelayIDs::offset, BDelayIDs::pingpong, BDelayIDs::mod, BDelayIDs::smear,
                                       BDelayIDs::duck, BDelayIDs::output, BDelayIDs::trails, BDelayIDs::bypass };
    constexpr float kSmearMs[4] = { 3.1f, 4.7f, 7.3f, 11.9f };
    inline float sin2 (float p) noexcept { const float s = std::sin (juce::MathConstants<float>::pi * p); return s * s; }
}

BDelayProcessor::BDelayProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BDelay", createLayout())
{
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    for (auto& b : buf) b.assign (kBuf, 0.0f);
    taps.reserve (8);
}

const juce::StringArray& BDelayProcessor::divisionNames()
{
    static const juce::StringArray n { "1/1", "1/1D", "1/1T", "1/2", "1/2D", "1/2T", "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T" };
    return n;
}

float BDelayProcessor::divisionBeats (int i) noexcept
{
    static constexpr float base[5] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    i = juce::jlimit (0, kDivisions - 1, i);
    const float b = base[i / 3];
    return (i % 3) == 1 ? b * 1.5f : (i % 3) == 2 ? b * 2.0f / 3.0f : b;
}

float BDelayProcessor::leftMs (float timeMs, bool sync, int division, double bpm) noexcept
{
    if (! sync) return timeMs;
    return juce::jlimit (1.0f, 2000.0f, divisionBeats (division) * (float) (60000.0 / juce::jlimit (30.0, 300.0, bpm)));
}

juce::AudioProcessorValueTreeState::ParameterLayout BDelayProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto skewed = [] (float a0, float a1, float step, float centre) { NormalisableRange<float> r (a0, a1, step); r.setSkewForCentre (centre); return r; };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BDelayIDs::type, 1 }, "Type", StringArray { "Digital", "Tape", "BBD", "Lofi", "Reverse" }, 0));
    f (BDelayIDs::time, "Time", skewed (1.0f, 2000.0f, 0.1f, 300.0f), 380.0f, "ms");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDelayIDs::sync, 1 }, "Sync", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BDelayIDs::division, 1 }, "Division", divisionNames(), 6));
    f (BDelayIDs::feedback, "Feedback", { 0.0f, 110.0f, 1.0f }, 45.0f, "%");
    f (BDelayIDs::locut, "Low cut", skewed (20.0f, 800.0f, 1.0f, 150.0f), 120.0f, "Hz");
    f (BDelayIDs::hicut, "High cut", skewed (800.0f, 20000.0f, 10.0f, 5000.0f), 6000.0f, "Hz");
    f (BDelayIDs::mix, "Mix", { 0.0f, 100.0f, 1.0f }, 30.0f, "%");
    f (BDelayIDs::offset, "Offset", { -50.0f, 50.0f, 1.0f }, 0.0f, "%");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDelayIDs::pingpong, 1 }, "Ping-pong", false));
    f (BDelayIDs::mod, "Mod", { 0.0f, 100.0f, 1.0f }, 20.0f, "%");
    f (BDelayIDs::smear, "Smear", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BDelayIDs::duck, "Duck", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BDelayIDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDelayIDs::trails, 1 }, "Trails", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BDelayIDs::bypass, 1 }, "Bypass", false));
    return l;
}

bool BDelayProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && out == juce::AudioChannelSet::stereo();
}

void BDelayProcessor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2500.0) taps.clear();   // pausa lunga: si ricomincia
    taps.push_back (now);
    if (taps.size() > 4) taps.erase (taps.begin());
    if (taps.size() < 2) return;
    const double ms = (taps.back() - taps.front()) / (double) (taps.size() - 1);   // media degli ultimi intervalli
    auto set = [this] (const char* id, float v) { auto* p = apvts.getParameter (id); p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); };
    set (BDelayIDs::sync, 0.0f);
    set (BDelayIDs::time, (float) juce::jlimit (1.0, 2000.0, ms));
}

void BDelayProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    w = 0;
    const float ms = leftMs (prm (P_TIME), prm (P_SYNC) > 0.5f, (int) prm (P_DIV), bpmNow.load());
    dSm[0] = ms * 0.001f * (float) fs; dSm[1] = dSm[0] * (1.0f + prm (P_OFFSET) / 100.0f);
    revPhase = { { { 0.0f, 0.5f }, { 0.0f, 0.5f } } };
    for (int c = 0; c < 2; ++c)
    {
        for (auto* f : { &lo[(size_t) c], &hi[(size_t) c], &bbd1[(size_t) c], &bbd2[(size_t) c], &lofiHp[(size_t) c], &lofiLp[(size_t) c] }) f->reset();
        bbd1[(size_t) c].set (fs, 2800.0, 0.7071); bbd2[(size_t) c].set (fs, 2800.0, 0.5412);
        lofiHp[(size_t) c].set (fs, 300.0); lofiLp[(size_t) c].set (fs, 3000.0);
        for (size_t k = 0; k < 4; ++k)
        {
            auto& a = smearAp[(size_t) c][k];
            a.len = juce::jmax (1, (int) std::round (kSmearMs[k] * (c == 0 ? 1.0f : 1.13f) * 0.001 * fs));
            a.b.assign ((size_t) a.len, 0.0f); a.pos = 0;
        }
    }
    tapeLp = { 0, 0 }; holdV = { 0, 0 }; holdN = 0; fbSig = { 0, 0 };
    auto osc = [this] (float hz, float& s, float& c, float& k) { k = 2.0f * std::sin (juce::MathConstants<float>::pi * hz / (float) fs); s = 0.0f; c = 1.0f; };
    osc (0.8f, modS, modC, modK); osc (0.5f, wowS, wowC, wowK); osc (6.0f, flS, flC, flK);
    fbSm = prm (P_FB) / 100.0f; mixSm = prm (P_MIX) / 100.0f; outSm = sp::dbToGain (prm (P_OUT)); sendSm = 1.0f;
    duckEnv = 0; duckAtk = sp::coefMs (10.0, fs); duckRel = sp::coefMs (250.0, fs);
    lastHi = lastLo = -1; lastType = -1;
    rIn = rL = rR = 0; rN = 0;
}

float BDelayProcessor::readHermite (const std::vector<float>& b, float delay) const noexcept
{
    const float pos = (float) w - delay + (float) kBuf;
    const int i = (int) pos;
    const float f = pos - (float) i;
    const float y0 = b[(size_t) ((i - 1) & (kBuf - 1))], y1 = b[(size_t) (i & (kBuf - 1))];
    const float y2 = b[(size_t) ((i + 1) & (kBuf - 1))], y3 = b[(size_t) ((i + 2) & (kBuf - 1))];
    const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float BDelayProcessor::character (int c, float x) noexcept
{
    switch (lastType)
    {
        case Tape:
        {
            x = sp::fastTanh (1.4f * x) / 1.4f;                 // il nastro comprime i picchi
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 9000.0f / (float) fs);
            tapeLp[(size_t) c] += a * (x - tapeLp[(size_t) c]);
            return tapeLp[(size_t) c];
        }
        case Bbd:
            x = bbd2[(size_t) c].processLP (bbd1[(size_t) c].processLP (x));
            return sp::fastTanh (1.2f * x) / 1.2f;
        case Lofi:
        {
            const int hold = juce::jmax (1, (int) std::round (fs / 12000.0));
            if (holdN % hold == 0) holdV[(size_t) c] = std::round (x * 127.0f) / 127.0f;   // 8 bit, ~12 kHz
            return lofiLp[(size_t) c].processLP (lofiHp[(size_t) c].processHP (holdV[(size_t) c]));
        }
        default: return x;
    }
}

void BDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    double bpm = 120.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    bpmNow.store (bpm, std::memory_order_relaxed);
    const int type = juce::jlimit (0, 4, (int) prm (P_TYPE));
    if (type != lastType) lastType = type;
    const float ms = leftMs (prm (P_TIME), prm (P_SYNC) > 0.5f, (int) prm (P_DIV), bpm);
    const float offs = prm (P_OFFSET) / 100.0f;
    const float maxD = (float) (kBuf - 8) * 0.5f;
    const float dT[2] = { juce::jlimit (4.0f, maxD, ms * 0.001f * (float) fs), juce::jlimit (4.0f, maxD, ms * (1.0f + offs) * 0.001f * (float) fs) };
    msL.store (ms, std::memory_order_relaxed); msR.store (ms * (1.0f + offs), std::memory_order_relaxed);
    const float lc = prm (P_LOCUT), hc = juce::jmin (prm (P_HICUT), (float) (0.45 * fs));
    if (std::abs (lc - lastLo) > 0.5f) { for (auto& f : lo) f.set (fs, lc); lastLo = lc; }
    if (std::abs (hc - lastHi) > 1.0f) { for (auto& f : hi) f.set (fs, hc); lastHi = hc; }
    const bool pp = prm (P_PP) > 0.5f;
    const float fbT = prm (P_FB) / 100.0f, mixT = prm (P_MIX) / 100.0f, outT = sp::dbToGain (prm (P_OUT));
    const float modD = prm (P_MOD) / 100.0f * 0.003f * (float) fs;   // fino a ±3 ms
    const float smear = prm (P_SMEAR) / 100.0f * 0.7f, duckAmt = prm (P_DUCK) / 100.0f;
    const bool bypassed = prm (P_BYPASS) > 0.5f, trails = prm (P_TRAILS) > 0.5f;
    const float sendT = bypassed ? 0.0f : 1.0f, wetOutT = bypassed && ! trails ? 0.0f : 1.0f;
    const float dCoef = 1.0f - std::exp (-1.0f / (0.08f * (float) fs));   // il tempo scivola in ~80 ms: repitch da nastro
    constexpr float a = 0.002f;
    float peak = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float xl = L[i], xr = inCh > 1 ? R[i] : L[i];
        modS += modK * modC; modC -= modK * modS;
        wowS += wowK * wowC; wowC -= wowK * wowS;
        flS += flK * flC; flC -= flK * flS;
        fbSm += a * (fbT - fbSm); mixSm += a * (mixT - mixSm); outSm += a * (outT - outSm);
        sendSm += a * (sendT - sendSm); wetOutSm += a * (wetOutT - wetOutSm);

        float proc[2];
        for (int c = 0; c < 2; ++c)
        {
            dSm[(size_t) c] += dCoef * (dT[c] - dSm[(size_t) c]);
            float d = dSm[(size_t) c] + modD * (c == 0 ? modS : modC);
            if (type == Tape) d += 0.0006f * (float) fs * wowS + 0.00005f * (float) fs * flS;
            d = juce::jlimit (3.0f, maxD, d);
            float r;
            if (type == Reverse)
            {
                // due testine che leggono all'indietro un grano lungo quanto il delay, incrociate a coseno
                r = 0.0f;
                for (auto& p : revPhase[(size_t) c])
                {
                    p += 1.0f / d;
                    if (p >= 1.0f) p -= 1.0f;
                    r += sin2 (p) * readHermite (buf[(size_t) c], 3.0f + 2.0f * p * d);
                }
            }
            else r = readHermite (buf[(size_t) c], d);
            float y = hi[(size_t) c].processLP (lo[(size_t) c].processHP (r));
            y = character (c, y);
            if (smear > 0.001f)
                for (auto& ap : smearAp[(size_t) c])
                {
                    const float del = ap.b[(size_t) ap.pos];
                    const float o = -smear * y + del;
                    ap.b[(size_t) ap.pos] = y + smear * o;
                    ap.pos = (ap.pos + 1) % ap.len;
                    y = o;
                }
            proc[c] = y;
        }
        ++holdN;
        // feedback con soft clipper (fino al 110 % si auto-oscilla senza esplodere)
        const float f0 = sp::fastTanh (fbSm * proc[0]), f1 = sp::fastTanh (fbSm * proc[1]);
        const float inL = sendSm * (pp ? 0.5f * (xl + xr) : xl), inR = sendSm * (pp ? 0.0f : xr);
        const float wl = inL + (pp ? f1 : f0), wr = inR + (pp ? f0 : f1);
        buf[0][(size_t) w] = wl; buf[1][(size_t) w] = wr;
        w = (w + 1) & (kBuf - 1);

        const float dry = std::abs (0.5f * (xl + xr));
        duckEnv = dry > duckEnv ? duckAtk * (duckEnv - dry) + dry : duckRel * (duckEnv - dry) + dry;
        const float duckG = duckAmt > 0.0f ? 1.0f - duckAmt * 0.85f * juce::jlimit (0.0f, 1.0f, (sp::gainToDb (duckEnv) + 45.0f) / 30.0f) : 1.0f;
        const float dryG = bypassed ? 1.0f : juce::jmin (1.0f, 2.0f * (1.0f - mixSm));
        const float wetG = juce::jmin (1.0f, 2.0f * mixSm) * duckG * wetOutSm;
        L[i] = (dryG * xl + wetG * proc[0]) * (bypassed ? 1.0f : outSm);
        R[i] = (dryG * xr + wetG * proc[1]) * (bypassed ? 1.0f : outSm);
        peak = juce::jmax (peak, std::abs (proc[0]), std::abs (proc[1]));

        rIn = juce::jmax (rIn, dry); rL = juce::jmax (rL, std::abs (wl)); rR = juce::jmax (rR, std::abs (wr));
        if (++rN >= kDecim)
        {
            const int p = (ringPos.load (std::memory_order_relaxed) + 1) & (kRing - 1);
            ringIn[(size_t) p] = rIn; ringL[(size_t) p] = rL; ringR[(size_t) p] = rR;
            ringPos.store (p, std::memory_order_release);
            rIn = rL = rR = 0; rN = 0;
        }
    }
    wetPeak.store (juce::jmax (peak, 0.85f * wetPeak.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    for (int c = 0; c < 2; ++c)
        for (auto* f : { &lo[(size_t) c], &hi[(size_t) c], &bbd1[(size_t) c], &bbd2[(size_t) c], &lofiHp[(size_t) c], &lofiLp[(size_t) c] }) f->flush();
}

void BDelayProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BDelayProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BDelayProcessor::createEditor() { return new BDelayEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BDelayProcessor(); }
