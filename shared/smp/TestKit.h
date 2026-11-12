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

// Aggiornamenti: confronto delle versioni, lettura delle release di GitHub, quadratino accanto al logo e
// pannello about con il tasto download (snapshot <file>_update.png e <file>_update_about.png).
inline void checkUpdates (juce::AudioProcessor& p, const juce::String& file)
{
    using namespace ui::updates;
    check (compareVersions ("0.10.0", "0.9.3") > 0 && compareVersions ("1.0", "1.0.0") == 0 && compareVersions ("0.1.0", "0.1.1") < 0,
           "aggiornamenti: confronto delle versioni (numeri, non testo)");
    const auto rel = juce::JSON::parse (R"([
        { "tag_name": "b-rig-v0.2.0", "html_url": "https://github.com/x/y/releases/tag/b-rig-v0.2.0", "draft": false, "prerelease": true },
        { "tag_name": "b-rig-v0.10.0", "html_url": "https://u/10", "draft": true, "prerelease": false },
        { "tag_name": "b-rig-v0.1.5", "html_url": "https://u/15", "draft": false, "prerelease": false },
        { "tag_name": "b-amp-v1.0.0", "html_url": "https://u/amp", "draft": false, "prerelease": false },
        { "tag_name": "notes", "html_url": "https://u/n", "draft": false, "prerelease": false } ])");
    const auto latest = latestPerProduct (rel);
    check (latest["b-rig"]["version"].toString() == "0.2.0" && latest["b-amp"]["version"].toString() == "1.0.0" && ! latest.hasProperty ("notes"),
           "aggiornamenti: versione più alta per prodotto (bozze escluse, pre-release incluse in beta)");

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditorAndMakeActive());
    ui::AboutOverlay* about = nullptr;
    for (auto* c : ed->getChildren()) if (auto* a = dynamic_cast<ui::AboutOverlay*> (c)) about = a;
    check (about != nullptr, "aggiornamenti: pannello about trovato");
    if (about == nullptr) return;
    about->setUpdateForTest ({ "0.2.0", "https://github.com/x/y/releases/tag/b-0.2.0" });
    for (int k = 0; k < 4; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
    auto save = [&] (const juce::String& name)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto f = juce::File::getCurrentWorkingDirectory().getChildFile ("build").getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        check (os.openedOk() && juce::PNGImageFormat().writeImageToStream (img, os), name.toRawUTF8());
    };
    check (about->toastVisibleForTest(), "aggiornamenti: avviso nell'angolo quando c'è una versione nuova");
    save (file + "_update.png");
    about->clickToastForTest (false);
    check (about->isVisible() && ! about->toastVisibleForTest(), "aggiornamenti: un clic sull'avviso apre il pannello about");
    save (file + "_update_about.png");
    about->setVisible (false);
    about->setUpdateForTest ({ "0.2.0", "https://github.com/x/y/releases/tag/b-0.2.0" });
    about->clickToastForTest (true);
    check (! about->isVisible() && ! about->toastVisibleForTest(), "aggiornamenti: la x chiude l'avviso senza aprire il pannello");
    p.editorBeingDeleted (ed.get());
}

inline int finish() { std::printf ("\n%s (%d falliti)\n", failures == 0 ? "TUTTI I TEST PASSATI" : "CI SONO TEST FALLITI", failures); return failures == 0 ? 0 : 1; }
} // namespace tk
