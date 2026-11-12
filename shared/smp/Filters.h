#pragma once
// Filtri di base condivisi (RBJ cookbook, forma diretta trasposta II), senza dipendenze da JUCE.
#include <cmath>

namespace sp
{

struct Bq
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
    void flush() noexcept { if (std::abs (z1) < 1e-20f) z1 = 0; if (std::abs (z2) < 1e-20f) z2 = 0; }

    enum Kind { LP, HP, BP, Peak, LowShelf, HighShelf };
    void set (Kind k, double fs, double f, double q, double gainDb = 0.0) noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * std::fmin (f, 0.49 * fs) / fs, c = std::cos (w), s = std::sin (w);
        const double A = std::pow (10.0, gainDb / 40.0), al = s / (2.0 * q);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (k)
        {
            case LP: B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case HP: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case BP: B0 = al; B1 = 0; B2 = -al; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case Peak: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            case LowShelf:
            {
                const double sa = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * c + sa); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sa);
                A0 = (A + 1) + (A - 1) * c + sa; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sa; break;
            }
            case HighShelf:
            {
                const double sa = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * c + sa); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sa);
                A0 = (A + 1) - (A - 1) * c + sa; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sa; break;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    // |H| alla frequenza f (per i grafici)
    double magnitude (double f, double fs) const noexcept
    {
        const double w = 2.0 * 3.14159265358979323846 * f / fs;
        const double cr = b0 + b1 * std::cos (w) + b2 * std::cos (2 * w), ci = -(b1 * std::sin (w) + b2 * std::sin (2 * w));
        const double dr = 1 + a1 * std::cos (w) + a2 * std::cos (2 * w), di = -(a1 * std::sin (w) + a2 * std::sin (2 * w));
        return std::sqrt ((cr * cr + ci * ci) / (dr * dr + di * di));
    }
};

// Passa-tutto del 1° ordine: ruota la fase senza cambiare il timbro.
struct Allpass1
{
    float a = 0, z = 0;
    void set (double fs, double f) noexcept { const double t = std::tan (3.14159265358979323846 * f / fs); a = (float) ((t - 1) / (t + 1)); }
    inline float process (float x) noexcept { const float y = a * x + z; z = x - a * y; return y; }
};

inline float coefMs (double ms, double fs) noexcept { return (float) std::exp (-1.0 / (ms * 0.001 * fs)); }

// tanh razionale (Padé 3/2) con clamp a ±3: continua, ~4x più veloce di std::tanh, errore < 2.5% solo vicino a |x|=1.5.
inline float fastTanh (float x) noexcept
{
    x = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// dB <-> lineare veloci (per inviluppi a control rate)
inline float dbToGain (float db) noexcept { return std::exp2 (db * 0.16609640474f); }          // 10^(db/20)
inline float gainToDb (float g) noexcept { return 6.0205999133f * std::log2 (g + 1.0e-12f); }   // 20*log10

// Stato-variabile TPT (Cytomic / Zavalishin): stabile sotto modulazione, nessun click quando cambia la
// frequenza perché lo stato non viene mai azzerato. setFreq() a control rate, process() per campione.
struct Svf
{
    float g = 0, k = 1.41421356f, a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    float lp = 0, bp = 0, hp = 0;
    void set (double fs, double f, double q = 0.70710678) noexcept
    {
        g = (float) std::tan (3.14159265358979323846 * std::fmin (f, 0.49 * fs) / fs);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    inline float processLP (float x) noexcept { tick (x); return lp; }
    inline float processHP (float x) noexcept { tick (x); return hp; }
    void reset() noexcept { ic1 = ic2 = lp = bp = hp = 0; }
    void flush() noexcept { if (std::abs (ic1) < 1e-20f) ic1 = 0; if (std::abs (ic2) < 1e-20f) ic2 = 0; }
};

} // namespace sp
