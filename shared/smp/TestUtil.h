#pragma once
// Verifica comune per i test della suite: nessun riquadro numerico cambia larghezza col valore.
// Per ogni ValueBox dell'editor si rende il valore più corto e quello più lungo (minimo, massimo,
// default e tutto il range a 64 passi): la larghezza ideale e quella reale devono restare identiche.
#include "Widgets.h"
#include <cstdio>

namespace ui
{
inline bool checkFixedValueBoxes (juce::Component& root, juce::String& report)
{
    bool ok = true;
    std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
    {
        if (auto* vb = dynamic_cast<ValueBox*> (&c); vb != nullptr && vb->getParameter() != nullptr && vb->isVisible())   // quelli nascosti (altre viste) no
        {
            auto* p = vb->getParameter();
            const float saved = p->getValue();
            const int w0 = vb->getWidth();
            int wMin = 1 << 30, wMax = 0;
            for (int i = 0; i <= 64; ++i)
            {
                p->setValueNotifyingHost ((float) i / 64.0f);
                const int w = vb->getIdealWidth();
                wMin = std::min (wMin, w); wMax = std::max (wMax, w);
            }
            p->setValueNotifyingHost (saved);
            if (wMin != wMax || wMax != w0)
            {
                ok = false;
                report << p->getName (40) << ": " << wMin << "-" << wMax << " px (box " << w0 << ")  ";
            }
        }
        for (auto* ch : c.getChildren()) visit (*ch);
    };
    visit (root);
    return ok;
}
} // namespace ui
