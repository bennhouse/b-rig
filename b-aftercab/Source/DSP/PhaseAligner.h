#pragma once
// Analisi per l'allineamento di fase tra il canale diretto e quello ritardato.
//
// Le due tracce sono x(n) e x(n - τ). Sommate (es. ascolto in mono) danno
// un'energia proporzionale a R(0) + R(τ), dove R è l'autocorrelazione del
// segnale. Le cancellazioni sono minime dove R(τ) è massima: cerchiamo quindi
// il massimo di R(τ) in una finestra intorno al delay scelto dall'utente, con
// precisione inferiore al campione (interpolazione parabolica del picco).

namespace sp
{

struct AlignResult
{
    bool ok = false;
    const char* error = "";      // motivo del fallimento (se ok == false)
    double delayMs = 0.0;        // delay consigliato
    double corrBefore = 0.0;     // correlazione normalizzata al delay di partenza (-1..1)
    double corrAfter = 0.0;      // correlazione normalizzata al delay trovato (-1..1)
};

// x: segnale catturato (mono), n campioni.
// Cerca in [currentMs - rangeMs, currentMs + rangeMs], limitato a [minMs, maxMs].
AlignResult findBestDelay (const float* x, int n, double sampleRate,
                           double currentMs, double rangeMs, double minMs, double maxMs);

} // namespace sp
