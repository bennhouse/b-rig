#pragma once
// Linea di ritardo con precisione inferiore al campione.
// Interpolazione sinc a 32 punti con finestra di Kaiser (tabella polifase):
// risposta piatta fino a ~20 kHz, a differenza di un'interpolazione lineare.
// Non aggiunge latenza: il ritardo minimo (1 ms) è già più lungo della metà
// del kernel (16 campioni), quindi non serve "guardare avanti".

#include <array>
#include <cmath>
#include <vector>

namespace sp
{

class FractionalDelay
{
public:
    static constexpr int kTaps = 32;
    static constexpr int kHalf = kTaps / 2;
    static constexpr int kPhases = 512;
    static constexpr double kMinDelaySamples = kHalf + 1;

    FractionalDelay() { buildTable(); }

    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + kTaps + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

    // Calcola i coefficienti per un ritardo (in campioni). Chiamare solo quando cambia.
    void setDelay (double delaySamples)
    {
        if (delaySamples < kMinDelaySamples) delaySamples = kMinDelaySamples;
        const double maxD = (double) (mask - kTaps - 2);
        if (delaySamples > maxD) delaySamples = maxD;

        const int di = (int) std::floor (delaySamples);
        const double f = delaySamples - di;
        // x(n - D) = x(base + mu), con base = n - di - 1 e mu = 1 - f
        intOffset = di + 1;
        const double pos = (1.0 - f) * kPhases;
        int p = (int) pos;
        if (p >= kPhases) p = kPhases - 1;
        const float t = (float) (pos - p);
        for (int j = 0; j < kTaps; ++j)
            kernel[(size_t) j] = table[(size_t) p][(size_t) j]
                               + t * (table[(size_t) p + 1][(size_t) j] - table[(size_t) p][(size_t) j]);
    }

    // Scrive un campione e restituisce l'uscita ritardata.
    inline float process (float x)
    {
        buffer[(size_t) writePos] = x;
        const int base = writePos - intOffset;
        float y = 0.0f;
        for (int j = 0; j < kTaps; ++j)
            y += kernel[(size_t) j] * buffer[(size_t) ((base - (kHalf - 1) + j) & mask)];
        writePos = (writePos + 1) & mask;
        return y;
    }

private:
    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1e-12 * sum) break;
        }
        return sum;
    }

    void buildTable()
    {
        const double beta = 9.0;
        const double i0b = besselI0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double mu = (double) p / kPhases;
            double sum = 0.0;
            std::array<double, kTaps> h {};
            for (int j = 0; j < kTaps; ++j)
            {
                const double x = mu - (j - (kHalf - 1));       // distanza dal punto da ricostruire
                const double sinc = std::abs (x) < 1e-12 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
                const double r = x / kHalf;
                const double w = std::abs (r) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - r * r)) / i0b;
                h[(size_t) j] = sinc * w;
                sum += h[(size_t) j];
            }
            for (int j = 0; j < kTaps; ++j)
                table[(size_t) p][(size_t) j] = (float) (h[(size_t) j] / sum);   // guadagno DC = 1
        }
    }

    std::array<std::array<float, kTaps>, kPhases + 1> table {};
    std::array<float, kTaps> kernel {};
    std::vector<float> buffer;
    int mask = 0, writePos = 0, intOffset = kHalf + 1;
};

} // namespace sp
