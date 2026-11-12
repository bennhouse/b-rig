#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <complex>

namespace
{
    enum { P_TYPE, P_DECAY, P_PRE, P_SIZE, P_LOCUT, P_HICUT, P_MIX, P_EARLY, P_DAMP, P_MOD, P_DUCK, P_STEREO, P_FREEZE, P_OUT };
    constexpr const char* kIds[14] = { BVerbIDs::type, BVerbIDs::decay, BVerbIDs::predelay, BVerbIDs::size, BVerbIDs::locut, BVerbIDs::hicut,
                                       BVerbIDs::mix, BVerbIDs::early, BVerbIDs::damp, BVerbIDs::mod, BVerbIDs::duck, BVerbIDs::stereo,
                                       BVerbIDs::freeze, BVerbIDs::output };
    constexpr float kDiffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
    constexpr float kTypeDiff[5] = { 0.5f, 0.75f, 0.6f, 0.0f, 0.6f };   // densità della diffusione per tipo
    constexpr float kInSign[8] = { 1, -1, 1, -1, -1, 1, -1, 1 };
    constexpr float kOutL[8] = { 1, -1, 1, -1, 1, -1, 1, -1 };
    constexpr float kOutR[8] = { 1, 1, -1, -1, 1, 1, -1, -1 };
    constexpr float kLfoHz[8] = { 0.21f, 0.33f, 0.47f, 0.29f, 0.61f, 0.38f, 0.83f, 0.53f };

    // trasformata di Hadamard veloce a 8 punti, normalizzata (solo somme e differenze, poi una scala)
    inline void hadamard8 (float* x) noexcept
    {
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; ++j) { const float a = x[j], b = x[j + h]; x[j] = a + b; x[j + h] = a - b; }
        for (int i = 0; i < 8; ++i) x[i] *= 0.35355339f;
    }
    inline float hannW (float p) noexcept { const float s = std::sin (juce::MathConstants<float>::pi * p); return s * s; }
    // lettura Hermite a 4 punti da un buffer circolare (dimensione potenza di 2)
    inline float hermite (const std::vector<float>& b, int mask, float pos) noexcept
    {
        const int i = (int) pos; const float f = pos - (float) i;
        const float y0 = b[(size_t) ((i - 1) & mask)], y1 = b[(size_t) (i & mask)], y2 = b[(size_t) ((i + 1) & mask)], y3 = b[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
}

// Shelf del 1° ordine (bilineare): a DC il guadagno per passaggio che dà l'RT60 "decay", a Nyquist il suo quadrato,
// transizione a "damp". Con freeze entrambi 1: il filtro diventa un filo.
BVerbProcessor::Shelf BVerbProcessor::absorbent (double lenSamples, double decay, double dampHz, double fs, bool frozen) noexcept
{
    const double gdc = frozen ? 1.0 : std::pow (10.0, -3.0 * lenSamples / (decay * fs)), ghf = gdc * gdc;
    const double k = std::tan (juce::MathConstants<double>::pi * juce::jlimit (20.0, 0.45 * fs, dampHz) / fs);
    return { (float) ((ghf + gdc * k) / (1.0 + k)), (float) ((gdc * k - ghf) / (1.0 + k)), (float) ((k - 1.0) / (1.0 + k)) };
}

double BVerbProcessor::rt60At (double hz, int type, float size01, double decay, double dampHz, double fs)
{
    // linea media: guadagno per passaggio alla frequenza, poi quanti passaggi servono per -60 dB
    double sumMs = 0; for (float ms : kLineMs) sumMs += ms;
    const double len = sumMs / 8.0 * 0.001 * fs * lineScale (juce::jlimit (0, 4, type), size01);
    const auto c = absorbent (len, decay, dampHz, fs, false);
    const auto z = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / fs);
    const double gain = std::abs (((double) c.b0 + (double) c.b1 * z) / (1.0 + (double) c.a1 * z));
    return gain >= 1.0 ? 1.0e3 : 3.0 * len / (-std::log10 (gain) * fs);
}

BVerbProcessor::BVerbProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BVerb", createLayout())
{
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = apvts.getRawParameterValue (kIds[i]);
    for (auto& b : inBuf) b.assign (kIn, 0.0f);
    for (auto& l : lines) l.assign (kLine, 0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout BVerbProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    auto skewed = [] (float lo, float hi, float step, float centre) { NormalisableRange<float> r (lo, hi, step); r.setSkewForCentre (centre); return r; };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BVerbIDs::type, 1 }, "Type", StringArray { "Room", "Plate", "Hall", "Spring", "Shimmer" }, 2));
    f (BVerbIDs::decay, "Decay", skewed (0.1f, 20.0f, 0.01f, 2.0f), 2.4f, "s");
    f (BVerbIDs::predelay, "Predelay", skewed (0.0f, 200.0f, 0.1f, 40.0f), 20.0f, "ms");
    f (BVerbIDs::size, "Size", { 0.0f, 100.0f, 1.0f }, 65.0f, "%");
    f (BVerbIDs::locut, "Low cut", skewed (20.0f, 500.0f, 1.0f, 120.0f), 100.0f, "Hz");
    f (BVerbIDs::hicut, "High cut", skewed (1000.0f, 20000.0f, 10.0f, 6000.0f), 10000.0f, "Hz");
    f (BVerbIDs::mix, "Mix", { 0.0f, 100.0f, 1.0f }, 25.0f, "%");
    f (BVerbIDs::early, "Early", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BVerbIDs::damp, "Damp", skewed (1000.0f, 16000.0f, 10.0f, 5000.0f), 6500.0f, "Hz");
    f (BVerbIDs::mod, "Mod", { 0.0f, 100.0f, 1.0f }, 30.0f, "%");
    f (BVerbIDs::duck, "Duck", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BVerbIDs::stereo, "Stereo", { 0.0f, 100.0f, 1.0f }, 100.0f, "%");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BVerbIDs::freeze, 1 }, "Freeze", false));
    f (BVerbIDs::output, "Output", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB");
    return l;
}

bool BVerbProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()) && out == juce::AudioChannelSet::stereo();
}

void BVerbProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    for (auto& b : inBuf) std::fill (b.begin(), b.end(), 0.0f);
    for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
    inPos = linePos = 0;
    zx.fill (0.0f); zy.fill (0.0f); erLp = { 0.0f, 0.0f };
    for (size_t k = 0; k < 4; ++k)
    {
        auto& a = diff[k];
        a.len = juce::jmax (1, (int) std::round (kDiffMs[k] * 0.001 * fs));
        a.b.assign ((size_t) a.len, 0.0f); a.pos = 0;
    }
    for (auto& a : springAp) { a.set (fs, 1200.0); a.z = 0.0f; }
    springLp.set (fs, 4500.0); springLp.reset();
    for (size_t i = 0; i < kLines; ++i)
    {
        const float w = juce::MathConstants<float>::twoPi * kLfoHz[i] / (float) fs;
        oscK[i] = 2.0f * std::sin (0.5f * w);   // oscillatore "magic circle": stabile, nessun seno per campione
        oscS[i] = std::sin ((float) i); oscC[i] = std::cos ((float) i);
    }
    std::fill (shBuf.begin(), shBuf.end(), 0.0f); shPos = 0;
    shGrain = (float) (0.06 * fs);
    shD = { 4.0f + shGrain, 4.0f + 0.5f * shGrain };
    shimmerFb = 0.0f;
    for (auto& f : loCut) f.reset();
    for (auto& f : hiCut) f.reset();
    duckEnv = 0; duckAtk = sp::coefMs (10.0, fs); duckRel = sp::coefMs (300.0, fs);
    mixSm = prm (P_MIX) / 100.0f; outSm = sp::dbToGain (prm (P_OUT)); widthSm = prm (P_STEREO) / 100.0f;
    type = -1; ctl = 0;
    control();
    len = lenT; b0 = b0T; b1 = b1T; a1 = a1T; tapD = tapDT; earlyG = earlyGT; preSm = preT;
    acc = {}; accN = 0;
}

// ogni 32 campioni: valori da inseguire (lunghezze, feedback per RT60, filtri, tap)
void BVerbProcessor::control() noexcept
{
    const int t = juce::jlimit (0, 4, (int) prm (P_TYPE));
    if (t != type)
    {
        type = t;
        for (size_t k = 0; k < 4; ++k) diff[k].g = kTypeDiff[t];
    }
    const float size = prm (P_SIZE) / 100.0f;
    const float scale = lineScale (t, size);
    const float decay = prm (P_DECAY);
    frozen = prm (P_FREEZE) > 0.5f;
    for (size_t i = 0; i < kLines; ++i)
    {
        lenT[i] = juce::jlimit (16.0f, (float) (kLine - 64), kLineMs[i] * 0.001f * (float) fs * scale);
        // assorbimento per linea: -60 dB in "decay" secondi sotto damp, in metà tempo sopra (freeze: nessuna perdita)
        const auto c = absorbent (lenT[i], decay, prm (P_DAMP), fs, frozen);
        b0T[i] = c.b0; b1T[i] = c.b1; a1T = c.a1;
    }
    erC = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * kTypeErHz[t] / (float) fs);
    preT = prm (P_PRE) * 0.001f * (float) fs;
    for (int k = 0; k < kTaps; ++k)
        for (int c = 0; c < 2; ++c) tapDT[(size_t) c][(size_t) k] = preT + tapTimeMs (t, size, k, c == 1) * 0.001f * (float) fs;
    earlyGT = prm (P_EARLY) / 100.0f * kTypeEarly[t] * 0.45f;
    modDepth = (prm (P_MOD) / 100.0f) * 10.0f * (float) (fs / 48000.0) * (t == Plate ? 0.5f : 1.0f);
    for (auto& f : loCut) f.set (fs, prm (P_LOCUT));
    for (auto& f : hiCut) f.set (fs, juce::jmin (prm (P_HICUT), (float) (0.45 * fs)));
}

void BVerbProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) return;
    const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    const float mixT = prm (P_MIX) / 100.0f, outT = sp::dbToGain (prm (P_OUT)), widthT = prm (P_STEREO) / 100.0f, duckAmt = prm (P_DUCK) / 100.0f;
    constexpr float a = 0.0015f;   // inseguimento dei valori (~15 ms a 48 kHz)

    auto readIn = [this] (int c, float d) noexcept
    {
        const float pos = (float) inPos - d + (float) kIn;
        const int i0 = (int) pos; const float f = pos - (float) i0;
        const auto& b = inBuf[(size_t) c];
        const float s0 = b[(size_t) (i0 & (kIn - 1))], s1 = b[(size_t) ((i0 + 1) & (kIn - 1))];
        return s0 + f * (s1 - s0);
    };

    for (int i = 0; i < n; ++i)
    {
        if (ctl-- <= 0) { control(); ctl = 31; }
        const float xl = L[i], xr = inCh > 1 ? R[i] : L[i];
        inBuf[0][(size_t) inPos] = xl; inBuf[1][(size_t) inPos] = xr;

        preSm += a * (preT - preSm);
        earlyG += a * (earlyGT - earlyG);
        // 2. prime riflessioni
        float er[2] = { 0.0f, 0.0f };
        if (earlyG > 1.0e-5f)
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < kTaps; ++k)
                {
                    auto& d = tapD[(size_t) c][(size_t) k];
                    d += a * (tapDT[(size_t) c][(size_t) k] - d);
                    er[c] += ((k & 1) ? -1.0f : 1.0f) * tapGain (k) * readIn (c, d);
                }
        // muri e aria assorbono gli acuti delle riflessioni
        erLp[0] += erC * (er[0] * earlyG - erLp[0]); erLp[1] += erC * (er[1] * earlyG - erLp[1]);
        er[0] = erLp[0]; er[1] = erLp[1];

        // 3. ingresso della coda: dopo il predelay, mono, diffuso
        float x = frozen ? 0.0f : 0.5f * (readIn (0, preSm) + readIn (1, preSm)) + shimmerFb;
        if (type == Spring)
        {
            for (auto& ap : springAp) x = ap.process (x);
            x = springLp.processLP (x);
        }
        else
            for (auto& d : diff)
            {
                const float del = d.b[(size_t) d.pos];
                const float y = -d.g * x + del;
                d.b[(size_t) d.pos] = x + d.g * y;
                d.pos = (d.pos + 1) % d.len;
                x = y;
            }

        // 4. FDN 8x8
        float v[kLines], out[kLines];
        a1 += a * (a1T - a1);
        for (size_t k = 0; k < kLines; ++k)
        {
            len[k] += a * (lenT[k] - len[k]);
            b0[k] += a * (b0T[k] - b0[k]);
            b1[k] += a * (b1T[k] - b1[k]);
            // oscillatore in quadratura per la modulazione lenta
            oscS[k] += oscK[k] * oscC[k]; oscC[k] -= oscK[k] * oscS[k];
            const float d = len[k] + (modDepth > 0.0f ? modDepth * (1.0f + oscS[k]) : 0.0f);
            out[k] = hermite (lines[k], kLine - 1, (float) linePos - d + (float) kLine);
            // shelf assorbente: RT60 esatto sotto "damp", metà sopra
            const float y = b0[k] * out[k] + b1[k] * zx[k] - a1 * zy[k];
            zx[k] = out[k]; zy[k] = y;
            v[k] = y;
        }
        hadamard8 (v);
        for (size_t k = 0; k < kLines; ++k) lines[k][(size_t) linePos] = v[k] + kInSign[k] * 0.35f * x;
        linePos = (linePos + 1) & (kLine - 1);

        float wl = 0, wr = 0;
        for (size_t k = 0; k < kLines; ++k) { wl += kOutL[k] * out[k]; wr += kOutR[k] * out[k]; }
        wl = 0.3f * wl + er[0]; wr = 0.3f * wr + er[1];

        // 5. shimmer: +1 ottava (due testine che si avvicinano alla scrittura al doppio della velocità)
        if (type == Shimmer)
        {
            shBuf[(size_t) shPos] = 0.5f * (wl + wr);
            float sh = 0.0f;
            for (auto& d : shD)
            {
                d -= 1.0f;   // rapporto 2: il ritardo cala di un campione per campione
                if (d < 4.0f) d += shGrain;
                const float p = (d - 4.0f) / shGrain;
                const float pos = (float) shPos - d + (float) shBuf.size();
                const int i0 = (int) pos; const float f = pos - (float) i0;
                const int m = (int) shBuf.size() - 1;
                sh += hannW (p) * (shBuf[(size_t) (i0 & m)] + f * (shBuf[(size_t) ((i0 + 1) & m)] - shBuf[(size_t) (i0 & m)]));
            }
            shPos = (shPos + 1) & ((int) shBuf.size() - 1);
            shimmerFb = 0.25f * sh;
        }
        else shimmerFb = 0.0f;

        // 7. uscita: filtri sul wet, larghezza, duck, mix
        wl = hiCut[0].processLP (loCut[0].processHP (wl));
        wr = hiCut[1].processLP (loCut[1].processHP (wr));
        widthSm += a * (widthT - widthSm);
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * widthSm;
        wl = mid + side; wr = mid - side;
        const float dry = std::abs (0.5f * (xl + xr));
        duckEnv = dry > duckEnv ? duckAtk * (duckEnv - dry) + dry : duckRel * (duckEnv - dry) + dry;
        const float duckG = duckAmt > 0.0f ? 1.0f - duckAmt * 0.85f * juce::jlimit (0.0f, 1.0f, (sp::gainToDb (duckEnv) + 45.0f) / 30.0f) : 1.0f;
        mixSm += a * (mixT - mixSm);
        outSm += a * (outT - outSm);
        const float dryG = juce::jmin (1.0f, 2.0f * (1.0f - mixSm)), wetG = juce::jmin (1.0f, 2.0f * mixSm) * duckG;
        L[i] = (dryG * xl + wetG * wl) * outSm;
        R[i] = (dryG * xr + wetG * wr) * outSm;
        inPos = (inPos + 1) & (kIn - 1);

        acc.in = juce::jmax (acc.in, std::abs (xl), std::abs (xr));
        acc.wet = juce::jmax (acc.wet, std::abs (wl), std::abs (wr));
        if (++accN >= 256)
        {
            const int p = (historyPos.load (std::memory_order_relaxed) + 1) & (kHistory - 1);
            history[(size_t) p] = acc;
            historyPos.store (p, std::memory_order_release);
            acc = {}; accN = 0;
        }
    }
    for (auto& v : zy) if (std::abs (v) < 1.0e-20f) v = 0.0f;
    for (auto& v : erLp) if (std::abs (v) < 1.0e-20f) v = 0.0f;
    for (auto& f : loCut) f.flush();
    for (auto& f : hiCut) f.flush();
    springLp.flush();
}

void BVerbProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void BVerbProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BVerbProcessor::createEditor() { return new BVerbEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BVerbProcessor(); }
