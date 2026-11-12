#pragma once
// Kit di test comune ai plugin della suite: controlli, esecuzione a blocchi, CPU, snapshot + larghezze fisse.
#include "TestUtil.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>

namespace tk
{
inline int failures = 0;
inline void check (bool ok, const char* what, double v = 0) { std::printf ("[%s] %s (%.6g)\n", ok ? " OK " : "FAIL", what, v); if (! ok) ++failures; }
constexpr double sr = 48000.0;
constexpr int block = 256;
inline const double tp = juce::MathConstants<double>::twoPi;

inline void setP (juce::AudioProcessorValueTreeState& s, const char* id, float v) { auto* a = s.getParameter (id); a->setValueNotifyingHost (a->convertTo0to1 (v)); }

template <typename P> std::unique_ptr<P> make (int ins = 1, int outs = 2)
{
    auto p = std::make_unique<P>();
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add (juce::AudioChannelSet::canonicalChannelSet (ins));
    l.outputBuses.add (juce::AudioChannelSet::canonicalChannelSet (outs));
    p->setBusesLayout (l);
    p->setRateAndBufferSizeDetails (sr, block);
    p->prepareToPlay (sr, block);
    return p;
}

// Segnale mono f(i) sul canale 0 (e 1 se stereo in ingresso); restituisce l'uscita.
template <typename F> juce::AudioBuffer<float> run (juce::AudioProcessor& p, int len, F sig)
{
    const int nOut = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    juce::AudioBuffer<float> out (nOut, len), b (nOut, block); juce::MidiBuffer m;
    for (int pos = 0; pos < len; pos += block)
    {
        const int n = std::min (block, len - pos);
        b.setSize (nOut, n, false, false, true); b.clear();
        for (int i = 0; i < n; ++i) { const float v = sig (pos + i); for (int c = 0; c < p.getTotalNumInputChannels(); ++c) b.setSample (c, i, v); }
        p.processBlock (b, m);
        for (int c = 0; c < nOut; ++c) out.copyFrom (c, pos, b, c, 0, n);
    }
    return out;
}

inline double rmsDb (const juce::AudioBuffer<float>& b, int ch, int from, int to)
{
    double e = 0; for (int i = from; i < to; ++i) e += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10 * std::log10 (e / (to - from) + 1e-30);
}

inline double toneDb (const juce::AudioBuffer<float>& b, int ch, int from, int to, double f)   // Goertzel, dB relativi all'ampiezza
{
    double s1 = 0, s2 = 0; const double c = 2 * std::cos (tp * f / sr);
    for (int i = from; i < to; ++i) { const double s0 = b.getSample (ch, i) + c * s1 - s2; s2 = s1; s1 = s0; }
    const double pw = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10 * std::log10 (pw + 1e-30) - 20 * std::log10 ((to - from) / 2.0);
}

// "chitarra": note pizzicate a f0 ogni periodo (s), armoniche che decadono
inline float pluck (int i, double f0, double period = 0.5, double amp = 0.3)
{
    const double t = std::fmod (i / sr, period);
    double s = 0; for (int h = 1; h <= 8; ++h) s += std::sin (tp * f0 * h * t) / h;
    return (float) (amp * s * std::exp (-t * 3.0));
}

template <typename F> double cpuPercent (juce::AudioProcessor& p, F sig, double seconds = 5.0)
{
    const int nOut = juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
    p.prepareToPlay (sr, 128);
    juce::AudioBuffer<float> b (nOut, 128); juce::MidiBuffer m; double t = 0;
    for (int k = 0; k < (int) (seconds * sr / 128); ++k)
    {
        b.clear();
        for (int i = 0; i < 128; ++i) { const float v = sig (k * 128 + i); for (int c = 0; c < p.getTotalNumInputChannels(); ++c) b.setSample (c, i, v); }
        const auto t0 = juce::Time::getHighResolutionTicks(); p.processBlock (b, m);
        t += juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    }
    p.prepareToPlay (sr, block);
    return 100.0 * t / seconds;
}

// Snapshot dell'interfaccia (dopo aver fatto girare il timer) + verifica delle larghezze fisse.
inline void snapshot (juce::AudioProcessor& p, const char* file, std::function<void()> beforeShot = {})
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
    { juce::String rep; check (ui::checkFixedValueBoxes (*ed, rep), ("riquadri a larghezza fissa (min/max di ogni valore) " + rep).toRawUTF8()); }
    if (beforeShot) beforeShot();
    for (int k = 0; k < 12; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile (file);
    f.deleteFile();
    juce::FileOutputStream os (f);
    check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), file);
    p.editorBeingDeleted (ed.get());   // come fa la DAW chiudendo la finestra: poi se ne può aprire un'altra
}

inline int finish() { std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures); return failures == 0 ? 0 : 1; }
} // namespace tk
