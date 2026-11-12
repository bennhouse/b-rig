#include "PhaseAligner.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace sp
{

AlignResult findBestDelay (const float* x, int n, double sampleRate,
                           double currentMs, double rangeMs, double minMs, double maxMs)
{
    AlignResult r;
    const double lo = std::max (minMs, currentMs - rangeMs);
    const double hi = std::min (maxMs, currentMs + rangeMs);
    const int lagMin = std::max (1, (int) std::ceil (lo * sampleRate / 1000.0));
    const int lagMax = (int) std::floor (hi * sampleRate / 1000.0);

    if (lagMax <= lagMin || n < 4 * (lagMax + 1))
    {
        r.error = "audio catturato insufficiente";
        return r;
    }

    // Finestra di somma fissa per tutti i lag, così i valori sono confrontabili.
    const int start = lagMax + 1;
    double energy = 0.0;
    for (int i = start; i < n; ++i) energy += (double) x[i] * x[i];
    const double rms = std::sqrt (energy / (n - start));
    if (rms < 1.0e-3)   // circa -60 dBFS
    {
        r.error = "segnale troppo debole o silenzio";
        return r;
    }

    auto corrAt = [&] (int lag)
    {
        double s = 0.0;
        for (int i = start; i < n; ++i) s += (double) x[i] * x[i - lag];
        return s;
    };

    std::vector<double> c ((size_t) (lagMax - lagMin + 1));
    int best = 0;
    for (int k = 0; k < (int) c.size(); ++k)
    {
        c[(size_t) k] = corrAt (lagMin + k);
        if (c[(size_t) k] > c[(size_t) best]) best = k;
    }

    // Interpolazione parabolica intorno al picco -> precisione sub-campione
    double offset = 0.0, peak = c[(size_t) best];
    if (best > 0 && best < (int) c.size() - 1)
    {
        const double ym = c[(size_t) best - 1], y0 = c[(size_t) best], yp = c[(size_t) best + 1];
        const double den = ym - 2.0 * y0 + yp;
        if (den < 0.0)
        {
            offset = std::clamp (0.5 * (ym - yp) / den, -0.5, 0.5);
            peak = y0 - 0.25 * (ym - yp) * offset;
        }
    }

    const double bestLag = lagMin + best + offset;
    const int curLag = (int) std::lround (currentMs * sampleRate / 1000.0);

    r.ok = true;
    r.delayMs = std::clamp (bestLag * 1000.0 / sampleRate, minMs, maxMs);
    r.corrAfter = peak / energy;
    r.corrBefore = corrAt (std::clamp (curLag, 1, lagMax + 1)) / energy;
    return r;
}

} // namespace sp
