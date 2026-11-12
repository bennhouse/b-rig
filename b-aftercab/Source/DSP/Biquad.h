#pragma once
// Filtro biquad (formule "Audio EQ Cookbook" di R. Bristow-Johnson).
// Nessuna allocazione: sicuro da usare nel thread audio.

#include <cmath>
#include <complex>

namespace sp
{

enum class FilterType { Bell, LowShelf, HighShelf, LowCut, HighCut };

struct BiquadCoeffs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;   // normalizzati (a0 = 1)

    static BiquadCoeffs make (FilterType type, double sampleRate, double freq, double q, double gainDb)
    {
        freq = std::fmin (std::fmax (freq, 10.0), 0.49 * sampleRate);
        q = std::fmax (q, 0.05);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * M_PI * freq / sampleRate;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0, b1, b2, a0, a1, a2;

        switch (type)
        {
            case FilterType::Bell:
                b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
                a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
                break;
            case FilterType::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + s);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - s);
                a0 = (A + 1) + (A - 1) * cw + s;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case FilterType::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + s);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - s);
                a0 = (A + 1) - (A - 1) * cw + s;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
            case FilterType::LowCut:   // passa-alto 12 dB/ott
                b0 = (1 + cw) / 2;  b1 = -(1 + cw);  b2 = (1 + cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
            case FilterType::HighCut:  // passa-basso 12 dB/ott
            default:
                b0 = (1 - cw) / 2;  b1 = 1 - cw;     b2 = (1 - cw) / 2;
                a0 = 1 + alpha;     a1 = -2 * cw;    a2 = 1 - alpha;
                break;
        }
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    // Modulo della risposta in frequenza alla frequenza normalizzata w (rad/campione)
    double magnitude (double w) const
    {
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        return std::abs ((b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2));
    }

    bool operator== (const BiquadCoeffs& o) const   // confronto bit a bit (rileva qualsiasi cambiamento)
    {
        auto same = [] (double x, double y) { return ! (x < y) && ! (y < x); };
        return same (b0, o.b0) && same (b1, o.b1) && same (b2, o.b2) && same (a1, o.a1) && same (a2, o.a2);
    }
};

struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0, z2 = 0;   // forma diretta II trasposta

    void reset() { z1 = z2 = 0; }

    inline float process (float xf)
    {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

} // namespace sp
