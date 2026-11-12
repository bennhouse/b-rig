#pragma once
// Analizzatore di spettro condiviso, veloce e adatto all'equalizzazione.
//
//  - SpectrumTap (thread audio): copia i campioni (mono) in un buffer circolare senza lock
//    e senza allocazioni. Costo: una scrittura per campione.
//  - SpectrumAnalyser (thread dell'interfaccia, ~30 fps): prende gli ultimi 8192 campioni,
//    finestra di Hann, FFT, pendenza +4,5 dB/ottava (lo spettro di una chitarra/mix appare
//    "piatto", come negli analizzatori da mixing), caduta morbida dei picchi.
//    Viene calcolato solo mentre l'interfaccia è aperta.

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace ui
{

class SpectrumTap
{
public:
    static constexpr int kSize = 1 << 14;   // 16384 campioni

    // Thread audio: aggiunge n campioni (media dei canali).
    void push (const float* const* ch, int numCh, int n) noexcept
    {
        int w = writePos.load (std::memory_order_relaxed);
        const float scale = numCh > 1 ? 0.5f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = ch[0][i];
            if (numCh > 1) s += ch[1][i];
            buffer[(size_t) w] = s * scale;
            w = (w + 1) & (kSize - 1);
        }
        writePos.store (w, std::memory_order_release);
    }

    // Thread interfaccia: copia gli ultimi n campioni (n <= kSize).
    void read (float* dest, int n) const noexcept
    {
        const int w = writePos.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i) dest[i] = buffer[(size_t) ((w - n + i) & (kSize - 1))];
    }

private:
    std::array<float, kSize> buffer {};
    std::atomic<int> writePos { 0 };
};

class SpectrumAnalyser
{
public:
    static constexpr int kOrder = 13, kFftSize = 1 << kOrder;   // 8192: ~5,9 Hz di risoluzione a 48 kHz

    SpectrumAnalyser() : fft (kOrder), window ((size_t) kFftSize), data ((size_t) (2 * kFftSize)), smoothed ((size_t) (kFftSize / 2 + 1), -120.0f)
    {
        for (int i = 0; i < kFftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kFftSize - 1));
    }

    // Aggiorna lo spettro (chiamare dal timer dell'interfaccia). fps = frequenza di aggiornamento.
    // Restituisce false quando lo spettro è tutto sotto "floorDb" (silenzio): niente da ridisegnare.
    bool update (const SpectrumTap& tap, double sampleRate, float fps = 30.0f, float floorDb = -90.0f)
    {
        sr = sampleRate > 0 ? sampleRate : 48000.0;
        tap.read (data.data(), kFftSize);
        for (int i = 0; i < kFftSize; ++i) data[(size_t) i] *= window[(size_t) i];
        std::fill (data.begin() + kFftSize, data.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (data.data(), true);

        const float norm = 4.0f / (float) kFftSize;         // finestra di Hann: ampiezza di picco di una sinusoide
        const float fall = 18.0f / fps;                     // i picchi scendono di ~18 dB/s
        if (std::abs (tiltSr - sr) > 0.5)                   // pendenza +4,5 dB/oct riferita a 1 kHz: calcolata una volta
        {
            tiltSr = sr;
            tilt.resize (smoothed.size());
            for (size_t k = 0; k < tilt.size(); ++k)
                tilt[k] = 4.5f * std::log2 (std::max ((float) ((double) k * sr / kFftSize), 20.0f) / 1000.0f);
        }
        bool any = false;
        for (size_t k = 1; k < smoothed.size(); ++k)
        {
            const float db = 20.0f * std::log10 (data[k] * norm + 1.0e-9f) + tilt[k];
            smoothed[k] = db > smoothed[k] ? db : std::max (db, smoothed[k] - fall);
            any = any || smoothed[k] > floorDb;
        }
        return any;
    }

    // Livello (dB, con pendenza) tra due frequenze: il massimo dei bin, o interpolazione se più stretto di un bin.
    float levelBetween (double f0, double f1) const
    {
        const double binHz = sr / kFftSize;
        const double b0 = f0 / binHz, b1 = f1 / binHz;
        const int last = (int) smoothed.size() - 1;
        if (b1 - b0 < 1.0)
        {
            const double b = juce::jlimit (1.0, (double) last - 1.0, 0.5 * (b0 + b1));
            const int i = (int) b;
            const float t = (float) (b - i);
            return smoothed[(size_t) i] + t * (smoothed[(size_t) i + 1] - smoothed[(size_t) i]);
        }
        float m = -120.0f;
        for (int i = juce::jlimit (1, last, (int) b0); i <= juce::jlimit (1, last, (int) b1); ++i) m = std::max (m, smoothed[(size_t) i]);
        return m;
    }

    // Disegna lo spettro come area piena tenue + contorno, dentro "area".
    // freqForX: mappa x -> frequenza; dB da topDb (in alto) a bottomDb (in basso).
    template <typename FreqForX>
    void draw (juce::Graphics& g, juce::Rectangle<float> area, FreqForX freqForX,
               juce::Colour colour, float topDb = 0.0f, float bottomDb = -84.0f,
               float fillAlpha = 0.10f, float lineAlpha = 0.35f) const
    {
        juce::Path fill, line;
        const float x0 = area.getX(), x1 = area.getRight();
        const double nyq = sr * 0.5;
        bool started = false;
        for (float x = x0; x <= x1; x += 1.0f)
        {
            const double fa = freqForX (x - 0.5f), fb = freqForX (x + 0.5f);
            if (fa >= nyq) break;
            const float db = levelBetween (fa, std::min (fb, nyq));
            const float y = juce::jlimit (area.getY(), area.getBottom(),
                                          juce::jmap (db, topDb, bottomDb, area.getY(), area.getBottom()));
            if (! started) { fill.startNewSubPath (x, area.getBottom()); fill.lineTo (x, y); line.startNewSubPath (x, y); started = true; }
            else { fill.lineTo (x, y); line.lineTo (x, y); }
        }
        if (! started) return;
        fill.lineTo (line.getCurrentPosition().x, area.getBottom());
        fill.closeSubPath();
        if (fillAlpha > 0.0f) { g.setColour (colour.withAlpha (fillAlpha)); g.fillPath (fill); }
        g.setColour (colour.withAlpha (lineAlpha));
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }

private:
    juce::dsp::FFT fft;
    std::vector<float> window, data, smoothed, tilt;
    double sr = 48000.0, tiltSr = 0.0;
};

} // namespace ui
