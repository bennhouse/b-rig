#include "PluginEditor.h"

using namespace ui;
namespace
{
    constexpr int kW = 900, kH = 640, kMargin = 24;
    const juce::Colour kDark { 0xff0d0d0f };
    const char* kNotes[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int tw (const juce::String& s) { return (int) std::ceil (juce::GlyphArrangement::getStringWidth (font (kLabelSize), s)); }
    juce::String noteOf (float hz)
    {
        if (hz <= 0) return "--";
        const int m = juce::roundToInt (69.0f + 12.0f * std::log2 (hz / 440.0f));
        return juce::String (kNotes[(m % 12 + 12) % 12]) + juce::String (m / 12 - 1);
    }
    float bandHz (int b) { return 40.0f * std::pow (300.0f, (float) b / (float) (VisualState::kBands - 1)); }   // 40 Hz - 12 kHz
}

juce::Colour BSynthesiaEditor::paletteColour (int i)
{
    static const juce::uint32 c[7] = { 0xffe2e8f0, 0xffd97706, 0xffe11d48, 0xff6366f1, 0xff84cc16, 0xff22d3ee, 0xff14b8a6 };
    return juce::Colour (c[juce::jlimit (0, 6, i)]);
}

// ============================================================================ scrittura dei fotogrammi
bool FrameWriter::push (juce::Image img, const juce::String& name)
{
    const juce::ScopedLock sl (lock);
    if (queue.size() >= 6) { ++dropped; return false; }
    queue.emplace_back (std::move (img), name);
    notify();
    return true;
}

void FrameWriter::run()
{
    while (! threadShouldExit())
    {
        std::pair<juce::Image, juce::String> job;
        { const juce::ScopedLock sl (lock); if (! queue.empty()) { job = std::move (queue.front()); queue.pop_front(); } }
        if (! job.first.isValid()) { wait (100); continue; }
        auto f = folder.getChildFile (job.second);
        juce::FileOutputStream os (f);
        if (os.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream (job.first, os); ++written; }
    }
}

// ============================================================================ tela
// Cinque scene generative in stile TouchDesigner. Le scene con scia (tunnel, flow, attractor, prism) lavorano in un
// buffer interno a bassa risoluzione (ARGB trasparente: funziona su ogni sfondo, anche chroma e alfa) aggiornato
// una volta per fotogramma da advance(); render() lo scala sulla tela (schermo o export) e aggiunge la parte vettoriale.
namespace
{
    inline juce::Colour hueColour (float h, float sat, float bright, float alpha)
    {
        return juce::Colour::fromHSV (h - std::floor (h), sat, bright, juce::jlimit (0.0f, 1.0f, alpha));
    }
    // somma "additiva" su un pixel ARGB premoltiplicato (luce che si accumula)
    inline void addPix (juce::Image::BitmapData& bd, int x, int y, juce::uint8 r, juce::uint8 g, juce::uint8 b, float amt)
    {
        if (x < 0 || y < 0 || x >= bd.width || y >= bd.height) return;
        auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, y));
        auto add = [amt] (juce::uint8 v, juce::uint8 c) { return (juce::uint8) juce::jmin (255, (int) v + (int) ((float) c * amt)); };
        p->setARGB (add (p->getAlpha(), 255), add (p->getRed(), r), add (p->getGreen(), g), add (p->getBlue(), b));
    }
    // dissolvenza della scia: tutti i canali (premoltiplicati) moltiplicati per f
    inline void fadeImage (juce::Image& img, float f)
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readWrite);
        const int k = juce::jlimit (0, 256, (int) (f * 256.0f));
        for (int y = 0; y < bd.height; ++y)
        {
            auto* row = bd.getLinePointer (y);
            for (int i = 0; i < bd.width * bd.pixelStride; ++i) row[i] = (juce::uint8) ((row[i] * k) >> 8);
        }
    }
    // campo di flusso pseudo-rumore (somma di seni, liscio e periodico)
    inline float flowAngle (float x, float y, float t, float chaos)
    {
        return 2.2f * std::sin (x * 3.1f + t * 0.35f) + 1.7f * std::cos (y * 2.7f - t * 0.27f) + (1.0f + 2.0f * chaos) * std::sin ((x + y) * 5.3f + t * 0.6f);
    }
}

void Canvas::ensureBuffer (int w, int h)
{
    if (fb.isValid() && fb.getWidth() == w && fb.getHeight() == h) return;
    fb = juce::Image (juce::Image::ARGB, w, h, true);
    fb2 = juce::Image (juce::Image::ARGB, w, h, true);
    parts.clear();
}

void Canvas::advance (float dt)
{
    t += dt;
    const int mode = (int) param (BSynIDs::mode);
    const float aspect = getHeight() > 0 ? (float) getWidth() / (float) getHeight() : 16.0f / 9.0f;
    const int bw = aspect >= 1.0f ? 640 : juce::roundToInt (640.0f * aspect), bh = aspect >= 1.0f ? juce::roundToInt (640.0f / aspect) : 640;
    ensureBuffer (juce::jmax (16, bw), juce::jmax (16, bh));
    if (mode != lastMode) { fb.clear (fb.getBounds()); fb2.clear (fb2.getBounds()); parts.clear(); lastMode = mode; }
    const float react = param (BSynIDs::reactivity) / 100.0f, chaos = param (BSynIDs::chaos) / 100.0f;
    const float decayMs = param (BSynIDs::decay);
    const float keep = std::exp (-dt * 1000.0f / (decayMs * 6.0f));   // scia: decay lungo = scia lunga
    const auto col = BSynthesiaEditor::paletteColour ((int) param (BSynIDs::colour));
    const float hue0 = col.getSaturation() < 0.1f ? t * 0.03f : col.getHue();
    const float W = (float) fb.getWidth(), H = (float) fb.getHeight();
    const float lvl = juce::jlimit (0.0f, 1.0f, (vs.level + 60.0f) / 60.0f);
    if (mode == 0)   // tunnel: il fotogramma precedente zoomato e ruotato + un poligono neon nuovo al centro
    {
        fb2.clear (fb2.getBounds());
        {
            juce::Graphics g (fb2);
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            const float zoom = 1.025f + 0.05f * vs.sub * react, rot = 0.006f + 0.05f * chaos * vs.hit + 0.004f * std::sin (t * 0.3f);
            g.setOpacity (keep);
            g.drawImageTransformed (fb, juce::AffineTransform::translation (-W * 0.5f, -H * 0.5f).scaled (zoom).rotated (rot).translated (W * 0.5f, H * 0.5f));
            g.setOpacity (1.0f);
            const int pc = vs.domHz > 0 ? juce::roundToInt (12.0f * std::log2 (vs.domHz / 440.0f) + 69.0f) : 0;
            const int sides = 3 + ((pc % 12 + 12) % 12) % 6;   // la nota suonata sceglie la forma (3..8 lati)
            const float rad = H * (0.05f + 0.10f * lvl * (0.5f + react));
            juce::Path poly;
            for (int k = 0; k <= sides; ++k)
            {
                const float an = t * 0.8f + juce::MathConstants<float>::twoPi * (float) k / (float) sides;
                const juce::Point<float> q (W * 0.5f + rad * std::cos (an), H * 0.5f + rad * std::sin (an));
                if (k == 0) poly.startNewSubPath (q); else poly.lineTo (q);
            }
            g.setColour (hueColour (hue0 + t * 0.05f, 0.85f, 1.0f, 0.35f + 0.6f * lvl));
            g.strokePath (poly, juce::PathStrokeType (1.5f + 4.0f * vs.sub * react));
            if (vs.hit > 0.25f)   // plettrata: raggi
            {
                g.setColour (hueColour (hue0 + 0.5f + t * 0.05f, 0.7f, 1.0f, 0.6f * vs.hit));
                for (int k = 0; k < 12; ++k)
                {
                    const float an = juce::MathConstants<float>::twoPi * (float) k / 12.0f + t;
                    g.drawLine (W * 0.5f + rad * std::cos (an), H * 0.5f + rad * std::sin (an), W * 0.5f + rad * 2.2f * std::cos (an), H * 0.5f + rad * 2.2f * std::sin (an), 1.2f);
                }
            }
        }
        std::swap (fb, fb2);
    }
    else if (mode == 1)   // flow: particelle in un campo di flusso, scie colorate
    {
        if (parts.empty()) { juce::Random r (3); parts.resize (2600); for (auto& p : parts) p = { r.nextFloat() * W, r.nextFloat() * H, 0, 0, r.nextFloat() }; }
        fadeImage (fb, keep);
        juce::Image::BitmapData bd (fb, juce::Image::BitmapData::readWrite);
        const float speed = (0.6f + 4.0f * lvl * (0.3f + react)) * W / 640.0f;
        for (auto& p : parts)
        {
            const float an = flowAngle (p.x / W, p.y / H, t, chaos);
            p.vx = 0.85f * p.vx + 0.15f * std::cos (an) * speed;
            p.vy = 0.85f * p.vy + 0.15f * std::sin (an) * speed;
            if (vs.hit > 0.3f)   // plettrata: spinta radiale dal centro
            {
                const float dx = p.x - W * 0.5f, dy = p.y - H * 0.5f, d = std::sqrt (dx * dx + dy * dy) + 1.0f;
                p.vx += dx / d * vs.hit * 3.0f * (0.3f + react); p.vy += dy / d * vs.hit * 3.0f * (0.3f + react);
            }
            p.x += p.vx; p.y += p.vy;
            if (p.x < 0 || p.x >= W || p.y < 0 || p.y >= H) { p.x = rng.nextFloat() * W; p.y = rng.nextFloat() * H; p.vx = p.vy = 0; }   // rinasce altrove
            const auto c = hueColour (hue0 + 0.35f * p.seed + 0.15f * std::sqrt (p.vx * p.vx + p.vy * p.vy) / (speed + 0.1f), 0.8f, 1.0f, 1.0f);
            addPix (bd, (int) p.x, (int) p.y, c.getRed(), c.getGreen(), c.getBlue(), 0.35f + 0.4f * lvl);
        }
    }
    else if (mode == 3)   // prism: caleidoscopio a 6 spicchi di un oscilloscopio polare, aberrazione RGB e tagli glitch
    {
        fadeImage (fb, keep * 0.8f);
        {
            juce::Graphics g (fb);
            const float R0 = H * 0.18f, amp = H * 0.22f * (0.4f + react);
            const float split = 2.0f + 10.0f * vs.hit * (0.3f + chaos);
            const juce::Colour rgb[3] = { juce::Colour (0xffff2d55), juce::Colour (0xff2dff9a), juce::Colour (0xff3d7bff) };
            for (int c = 0; c < 3; ++c)
            {
                juce::Path p;
                const int N = 180;
                for (int i = 0; i <= N; ++i)
                {
                    const float u = (float) i / (float) N;
                    const float an = juce::MathConstants<float>::twoPi / 6.0f * (u < 0.5f ? u * 2.0f : (1.0f - u) * 2.0f);   // spicchio specchiato
                    const float v = vs.at (0, (int) (u * 1200.0f));
                    const float rr = R0 + amp * v * 3.0f;
                    if (i == 0) p.startNewSubPath (rr * std::cos (an), rr * std::sin (an)); else p.lineTo (rr * std::cos (an), rr * std::sin (an));
                }
                g.setColour (rgb[c].withAlpha (0.55f));
                for (int k = 0; k < 6; ++k)
                    g.strokePath (p, juce::PathStrokeType (1.4f), juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * (float) k / 6.0f + t * 0.15f)
                                                                       .translated (W * 0.5f + (float) (c - 1) * split, H * 0.5f));
            }
        }
        if (vs.hit > 0.35f && chaos > 0.02f)   // tagli orizzontali spostati (datamosh)
        {
            fb2.clear (fb2.getBounds());
            { juce::Graphics g2 (fb2); g2.drawImageAt (fb, 0, 0); }
            juce::Graphics g (fb);
            const int n = 2 + (int) (6.0f * chaos);
            for (int k = 0; k < n; ++k)
            {
                const int y = rng.nextInt (fb.getHeight() - 8), h = 2 + rng.nextInt (14), dx = (int) ((rng.nextFloat() - 0.5f) * 60.0f * chaos * vs.hit);
                g.saveState(); g.reduceClipRegion (0, y, fb.getWidth(), h);
                g.setColour (juce::Colours::transparentBlack); g.getInternalContext().fillRect ({ 0, y, fb.getWidth(), h }, true);
                g.drawImageAt (fb2, dx, 0); g.restoreState();
            }
        }
    }
    else if (mode == 4)   // attractor: Clifford, parametri guidati dall'audio, decine di migliaia di punti accumulati
    {
        fadeImage (fb, keep * 0.9f);
        juce::Image::BitmapData bd (fb, juce::Image::BitmapData::readWrite);
        float lo = 0, mi = 0, hi = 0;
        for (int b = 0; b < 16; ++b) lo += vs.bands[(size_t) b] / 16.0f;
        for (int b = 16; b < 32; ++b) mi += vs.bands[(size_t) b] / 16.0f;
        for (int b = 32; b < 48; ++b) hi += vs.bands[(size_t) b] / 16.0f;
        // i parametri scorrono lenti e si piegano con bassi, medie e alte
        const float pa = -1.4f + 0.25f * std::sin (t * 0.11f) + 0.4f * lo * react, pb = 1.6f + 0.2f * std::cos (t * 0.07f) + 0.3f * mi * react;
        const float pc = 1.0f + 0.3f * std::sin (t * 0.05f) + 0.5f * hi * chaos, pd = 0.7f + 0.2f * std::cos (t * 0.13f) + 0.3f * vs.sub;
        const float s = H * 0.21f;
        const auto c = col.getSaturation() < 0.1f ? juce::Colour (0xffe2e8f0) : col;
        const float amt = 0.06f + 0.2f * lvl + 0.25f * vs.hit;
        for (int i = 0; i < 30000; ++i)
        {
            const float nx = std::sin (pa * ay) + pc * std::cos (pa * ax), ny = std::sin (pb * ax) + pd * std::cos (pb * ay);
            ax = nx; ay = ny;
            addPix (bd, (int) (W * 0.5f + ax * s), (int) (H * 0.5f + ay * s), c.getRed(), c.getGreen(), c.getBlue(), amt);
        }
    }
}

void Canvas::render (juce::Graphics& g, juce::Rectangle<float> a, bool forExport)
{
    const int bg = (int) param (BSynIDs::bg);
    // sfondo: scuro, chroma key puri (nessuna sfumatura ai bordi), alfa = trasparente nei PNG (a schermo: scacchiera tenue)
    if (bg == 1) { g.setColour (juce::Colour (0xff00ff00)); g.fillRect (a); }
    else if (bg == 2) { g.setColour (juce::Colour (0xffff00ff)); g.fillRect (a); }
    else if (bg == 3 && ! forExport)
    {
        g.setColour (kDark); g.fillRect (a);
        g.setColour (juce::Colour (0xff17171a));
        for (float y = a.getY(); y < a.getBottom(); y += 16.0f)
            for (float x = a.getX() + (std::fmod (y - a.getY(), 32.0f) < 16.0f ? 0.0f : 16.0f); x < a.getRight(); x += 32.0f) g.fillRect (x, y, 16.0f, 16.0f);
    }
    else if (bg == 0) { g.setColour (kDark); g.fillRect (a); }
    const auto col = BSynthesiaEditor::paletteColour ((int) param (BSynIDs::colour));
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (a.toNearestInt());
    const int mode = (int) param (BSynIDs::mode);
    if (mode != 2 && fb.isValid())   // buffer con la scia, scalato a riempire la tela
    {
        g.setImageResamplingQuality (bg == 1 || bg == 2 ? juce::Graphics::lowResamplingQuality : juce::Graphics::mediumResamplingQuality);
        g.drawImage (fb, a, juce::RectanglePlacement::fillDestination);
    }
    if (mode == 2) terrain (g, a, col, bg);
    // testo / griglia (parametro grid): una riga di telemetria discreta in basso a sinistra
    const float gridA = param (BSynIDs::grid) / 100.0f;
    if (gridA > 0.05f)
    {
        const char* names[5] = { "tunnel", "flow", "terrain", "prism", "attractor" };
        drawAtBaseline (g, juce::String (names[juce::jlimit (0, 4, mode)]) + "   " + noteOf (vs.domHz) + "   sub " + juce::String (juce::roundToInt (vs.sub * 100.0f))
                            + "   hit " + juce::String (juce::roundToInt (vs.hit * 100.0f)),
                        a.getX() + 16.0f, a.getBottom() - 16.0f, (col.getSaturation() < 0.1f ? juce::Colour (0xffe2e8f0) : col).withAlpha (0.3f + 0.5f * gridA), kAxisSmall, juce::Justification::left, Face::MonoRegular);
    }
}

// [ terrain ]: l'iso-mesh diventato un paesaggio 3D in prospettiva: spettro specchiato dal centro, storia che scorre
// verso chi guarda, facce piene (le creste davanti coprono quelle dietro), sole a scanline all'orizzonte che pulsa coi
// palm-mute, creste accese dalle plettrate. Monocromo nel colore scelto.
void Canvas::terrain (juce::Graphics& g, juce::Rectangle<float> a, juce::Colour col, int bg)
{
    const float react = param (BSynIDs::reactivity) / 100.0f, chaos = param (BSynIDs::chaos) / 100.0f, gridA = param (BSynIDs::grid) / 100.0f;
    const auto c0 = col.getSaturation() < 0.1f ? juce::Colour (0xffe2e8f0) : col;
    constexpr int R = VisualState::kRows, C = VisualState::kBands;
    const float horizon = a.getY() + a.getHeight() * 0.42f;
    // sole
    {
        const float sr = a.getHeight() * (0.16f + 0.05f * vs.sub * react);
        const auto sun = juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre ({ a.getCentreX(), horizon - sr * 0.35f });
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (juce::Rectangle<float> (a.getX(), a.getY(), a.getWidth(), horizon - a.getY()).toNearestInt());
        juce::ColourGradient grad (c0.withAlpha (0.85f), sun.getCentreX(), sun.getY(), c0.withAlpha (0.15f), sun.getCentreX(), sun.getBottom(), false);
        g.setGradientFill (grad); g.fillEllipse (sun);
        const juce::Colour cut = bg == 1 ? juce::Colour (0xff00ff00) : (bg == 2 ? juce::Colour (0xffff00ff) : kDark);
        g.setColour (cut);
        for (int k = 0; k < 7; ++k)   // tagli orizzontali del sole, sempre più spessi verso il basso, che scorrono
        {
            const float y = sun.getCentreY() + std::fmod (t * 12.0f + (float) k * 10.0f, 70.0f) * sun.getHeight() / 140.0f;
            g.fillRect (sun.getX(), y, sun.getWidth(), 1.0f + (float) k * 0.6f);
        }
    }
    // proiezione prospettica: colonna c (-1..1, spettro specchiato), riga r (0 = davanti), altezza h
    auto proj = [&] (float x, float z, float h)
    {
        const float persp = 1.0f / (0.25f + z * 3.0f);   // davanti 4x, in fondo 0,3x: le righe lontane toccano l'orizzonte
        return juce::Point<float> (a.getCentreX() + x * a.getWidth() * 0.3f * persp, horizon + (a.getHeight() * 0.14f - h * 0.3f) * persp);
    };
    std::array<std::array<juce::Point<float>, C>, R> P;
    for (int r = 0; r < R; ++r)
    {
        const auto& row = vs.history[(size_t) ((vs.histPos - r + R * 4) % R)];
        for (int c = 0; c < C; ++c)
        {
            const float x = -1.0f + 2.0f * (float) c / (float) (C - 1);
            const int band = juce::jlimit (0, C - 1, (int) (std::abs (x) * (float) (C - 1)));   // bassi al centro, alte ai lati
            const float ripple = vs.sub * react * a.getHeight() * 0.08f * std::sin (std::abs (x) * 9.0f - t * 7.0f + (float) r * 0.5f);
            const float h = row[(size_t) (C - 1 - band)] * a.getHeight() * 0.35f * (0.3f + react) + ripple + (chaos > 0 ? (rng.nextFloat() - 0.5f) * chaos * vs.hit * 20.0f : 0.0f);
            P[(size_t) r][(size_t) c] = proj (x * (1.0f + 0.15f * std::sin (t * 0.2f)), (float) r / (float) (R - 1), h);
        }
    }
    const juce::Colour fill = bg == 1 ? juce::Colour (0xff00ff00) : (bg == 2 ? juce::Colour (0xffff00ff) : kDark);
    for (int r = R - 2; r >= 0; --r)   // dal fondo verso chi guarda: ogni striscia copre quelle dietro
    {
        juce::Path strip;
        for (int c = 0; c < C; ++c) if (c == 0) strip.startNewSubPath (P[(size_t) r][(size_t) c]); else strip.lineTo (P[(size_t) r][(size_t) c]);
        for (int c = C - 1; c >= 0; --c) strip.lineTo (P[(size_t) r + 1][(size_t) c]);
        strip.closeSubPath();
        g.setColour (fill); g.fillPath (strip);
        const float near = 1.0f - (float) r / (float) R;
        juce::Path line;
        for (int c = 0; c < C; ++c) if (c == 0) line.startNewSubPath (P[(size_t) r][(size_t) c]); else line.lineTo (P[(size_t) r][(size_t) c]);
        g.setColour (c0.withAlpha (0.08f * near + (r < 3 ? 0.25f * vs.hit : 0.0f)));
        g.strokePath (line, juce::PathStrokeType (5.0f));   // alone
        g.setColour (c0.withAlpha (0.2f + 0.75f * near * near + (r < 3 ? 0.3f * vs.hit : 0.0f)));
        g.strokePath (line, juce::PathStrokeType (r == 0 ? 1.8f + vs.hit * 2.0f : 1.1f));
    }
    if (gridA > 0.02f)   // linee longitudinali verso l'orizzonte
        for (int c = 0; c < C; c += 4)
        {
            juce::Path line;
            for (int r = 0; r < R; ++r) if (r == 0) line.startNewSubPath (P[(size_t) r][(size_t) c]); else line.lineTo (P[(size_t) r][(size_t) c]);
            g.setColour (c0.withAlpha (0.18f * gridA)); g.strokePath (line, juce::PathStrokeType (1.0f));
        }
}

// ============================================================================ pulsanti
void TextButton2::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const bool on = isOn && isOn();
    if (on) { g.setColour (caption.startsWith ("rec") ? juce::Colour (0xffff4d4d) : accent); g.fillRect (r); }
    else { g.setColour (isMouseOver() ? labelHover : text); outline (g, r); }
    drawLabel (g, caption, getLocalBounds(), on ? onAccent : selector, kControlTextSize, juce::Justification::centred, Face::SansSemiBold);
}

// ============================================================================ editor
BSynthesiaEditor::BSynthesiaEditor (BSynthesiaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), canvas (p, vs),
      modeChoice (*p.apvts.getParameter (BSynIDs::mode), { "tunnel", "flow", "terrain", "prism", "attractor" }),
      colourChoice (*p.apvts.getParameter (BSynIDs::colour), { "white", "amber", "crimson", "indigo", "lime", "cyan", "teal" }),
      aspectChoice (*p.apvts.getParameter (BSynIDs::aspect), { "full", "16:9", "9:16" }),
      bgChoice (*p.apvts.getParameter (BSynIDs::bg), { "dark", "green", "magenta", "alpha" })
{
    const char* ids[4] = { BSynIDs::reactivity, BSynIDs::decay, BSynIDs::chaos, BSynIDs::grid };
    for (int i = 0; i < 4; ++i)
    {
        auto& vb = boxes[(size_t) i]; auto* prm = p.apvts.getParameter (ids[i]);
        vb.setParameter (prm);
        vb.format = i == 1 ? std::function<juce::String (float)> ([] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; }) : [] (float v) { return juce::String (juce::roundToInt (v)) + " %"; };
        vb.step = i == 1 ? std::function<float (float, int)> ([] (float v, int d) { return std::round (v * std::pow (1.15f, (float) d)); }) : [] (float v, int d) { return std::round (v) + 5.0f * (float) d; };
        vb.drag = [prm] (float, float s, float dx) { return prm->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, s + dx * 0.003f)); };
        vb.sizeSamples = { i == 1 ? "1000 ms" : "100 %" };
        addAndMakeVisible (vb);
    }
    for (auto* c : { (juce::Component*) &modeChoice, (juce::Component*) &colourChoice, (juce::Component*) &aspectChoice, (juce::Component*) &bgChoice }) addAndMakeVisible (c);
    cleanBtn.caption = "clean view"; cleanBtn.onClick = [this] { setClean (true); };
    recBtn.caption = "rec sync"; recBtn.isOn = [this] { return recArmed; };
    recBtn.onClick = [this]
    {
        recArmed = ! recArmed;
        if (recArmed)
        {
            writer.folder = juce::File::getSpecialLocation (juce::File::userMoviesDirectory).getChildFile ("b-synthesia")
                                .getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S"));
            writer.folder.createDirectory(); writer.written = 0; writer.dropped = 0; frameCounter = 0;
            if (! writer.isThreadRunning()) writer.startThread (juce::Thread::Priority::low);
        }
        repaint();
    };
    addAndMakeVisible (cleanBtn); addAndMakeVisible (recBtn);
    canvas.onClick = [this] { if (clean) setClean (false); };
    addAndMakeVisible (canvas);
    for (int i = 0; i < VisualState::kN; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (VisualState::kN - 1)));
    setResizable (true, true);
    setResizeLimits (640, 480, 2560, 1600);
    setSize (kW, kH);
    startTimerHz (60);
}

BSynthesiaEditor::~BSynthesiaEditor() { stopTimer(); writer.stopThread (3000); }

void BSynthesiaEditor::setClean (bool c)
{
    clean = c;
    for (auto* comp : getChildren()) if (comp != &canvas) comp->setVisible (! c);
    resized(); repaint();
}

juce::Rectangle<int> BSynthesiaEditor::canvasArea() const
{
    auto r = clean ? getLocalBounds() : getLocalBounds().withTrimmedTop (156).reduced (kMargin, 0).withTrimmedBottom (kMargin);
    const int asp = (int) proc.apvts.getRawParameterValue (BSynIDs::aspect)->load();
    if (asp == 0) return r;
    const float ratio = asp == 1 ? 16.0f / 9.0f : 9.0f / 16.0f;
    const float w = juce::jmin ((float) r.getWidth(), (float) r.getHeight() * ratio);
    return r.withSizeKeepingCentre (juce::roundToInt (w), juce::roundToInt (w / ratio));
}

void BSynthesiaEditor::resized()
{
    labels.clear();
    auto hdr = juce::Rectangle<int> (kMargin, headerRowY(), getWidth() - 2 * kMargin, kControlH);
    modeChoice.setBounds (hdr.removeFromRight (modeChoice.getIdealWidth()));
    hdr.removeFromRight (kGapLabel); labels.push_back ({ "mode", hdr.removeFromRight (tw ("mode")) });
    auto row = juce::Rectangle<int> (kMargin, 78, getWidth() - 2 * kMargin, kControlH);
    const char* names[4] = { "reactivity", "decay", "chaos", "grid" };
    for (int i = 0; i < 4; ++i)
    {
        labels.push_back ({ names[i], row.removeFromLeft (tw (names[i])) }); row.removeFromLeft (kGapLabel);
        boxes[(size_t) i].setBounds (row.removeFromLeft (boxes[(size_t) i].getIdealWidth())); row.removeFromLeft (kGapGroup);
    }
    recBtn.setBounds (juce::Rectangle<int> (getWidth() - kMargin - 84, 78, 84, kControlH));
    cleanBtn.setBounds (recBtn.getBounds().translated (-100, 0).withWidth (92));
    auto row2 = juce::Rectangle<int> (kMargin, 78 + kControlH + kGapInner, getWidth() - 2 * kMargin, kControlH);
    for (auto [name, comp] : { std::pair<const char*, ui::SegmentedChoice*> { "color", &colourChoice }, { "aspect", &aspectChoice }, { "bg", &bgChoice } })
    {
        labels.push_back ({ name, row2.removeFromLeft (tw (name)) }); row2.removeFromLeft (kGapLabel);
        comp->setBounds (row2.removeFromLeft (comp->getIdealWidth())); row2.removeFromLeft (kGapGroup);
    }
    canvas.setBounds (canvasArea());
}

void BSynthesiaEditor::paint (juce::Graphics& g)
{
    g.fillAll (kDark);
    if (clean) return;
    drawTitle (g, "synthesia", { kMargin - 1, 16, 400, 36 }, 30.0f);
    for (auto& [t, r] : labels) drawLabel (g, t, r, labelStatic, kLabelSize);
    if (recArmed)
        drawAtBaseline (g, (proc.playing.load() ? "recording  " : "armed, waiting for play  ") + juce::String (writer.written.load()) + " frames"
                            + (writer.dropped.load() > 0 ? "  (" + juce::String (writer.dropped.load()) + " dropped)" : juce::String()),
                        (float) getWidth() - kMargin, 78.0f + kControlH + kGapInner + 14.0f, juce::Colour (0xffff4d4d), kAxisSmall, juce::Justification::right, Face::MonoRegular);
}

void BSynthesiaEditor::analyse()
{
    // campioni dal FIFO
    int s1, n1, s2, n2;
    proc.fifo.prepareToRead (proc.fifo.getNumReady(), s1, n1, s2, n2);
    auto take = [&] (int st, int c) { for (int i = 0; i < c; ++i) { vs.roll[0][(size_t) vs.w] = proc.fifoBuf.getSample (0, st + i); vs.roll[1][(size_t) vs.w] = proc.fifoBuf.getSample (1, st + i); vs.w = (vs.w + 1) & (VisualState::kRoll - 1); } };
    take (s1, n1); take (s2, n2);
    proc.fifo.finishedRead (n1 + n2);
    // inviluppi visivi con "decay" e "reactivity"
    const float decayMs = proc.apvts.getRawParameterValue (BSynIDs::decay)->load(), react = proc.apvts.getRawParameterValue (BSynIDs::reactivity)->load() / 100.0f;
    const float k = 1.0f - std::exp (-1000.0f / (60.0f * decayMs));
    auto follow = [k] (float& v, float target) { v = target > v ? target : v + k * (target - v); };
    follow (vs.sub, proc.subEnergy.load() * (0.5f + react));
    follow (vs.hit, proc.transientHit.load() * (0.3f + react));
    vs.level = proc.levelDb.load(); vs.width = proc.stereoWidth.load();
    // FFT 4096 su (L+R)/2, bande log, picchi
    for (int i = 0; i < VisualState::kN; ++i) fftData[(size_t) i] = 0.5f * (vs.at (0, VisualState::kN - 1 - i) + vs.at (1, VisualState::kN - 1 - i)) * window[(size_t) i];
    std::fill (fftData.begin() + VisualState::kN, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0, binHz = fs / VisualState::kN;
    auto dbAt = [&] (int b) { return 20.0f * std::log10 (fftData[(size_t) b] * 4.0f / (float) VisualState::kN + 1e-9f); };
    auto& row = vs.history[(size_t) ((vs.histPos + 1) % VisualState::kRows)];
    for (int b = 0; b < VisualState::kBands; ++b)
    {
        const float f0 = bandHz (b), f1 = bandHz (b + 1);
        float mx = -120.0f;
        for (int bin = juce::jmax (1, (int) (f0 / binHz)); bin <= juce::jmin (VisualState::kN / 2 - 1, (int) (f1 / binHz) + 1); ++bin) mx = juce::jmax (mx, dbAt (bin));
        const float v = juce::jlimit (0.0f, 1.0f, (mx + 72.0f) / 72.0f);
        follow (vs.bands[(size_t) b], v);
        row[(size_t) b] = vs.bands[(size_t) b];
    }
    static int frameMod = 0; if (++frameMod % 3 == 0) vs.histPos = (vs.histPos + 1) % VisualState::kRows;   // la mesh scorre a 20 righe/s
    // picchi: massimi locali dei bin tra 60 Hz e 8 kHz, separati di almeno 1/3 d'ottava
    std::array<VisualState::Peak, 5> best {}; int nb = 0;
    for (int bin = (int) (60.0 / binHz) + 1; bin < (int) (8000.0 / binHz); ++bin)
    {
        const float d = dbAt (bin);
        if (d < -60.0f || d < dbAt (bin - 1) || d < dbAt (bin + 1)) continue;
        const float hz = (float) (bin * binHz);
        bool near = false;
        for (int j = 0; j < nb; ++j) if (std::abs (std::log2 (hz / best[(size_t) j].hz)) < 0.33f) { near = true; if (d > best[(size_t) j].db) best[(size_t) j] = { hz, d, 0 }; break; }
        if (near) continue;
        if (nb < 5) best[(size_t) nb++] = { hz, d, 0 };
        else { int w = 0; for (int j = 1; j < 5; ++j) if (best[(size_t) j].db < best[(size_t) w].db) w = j; if (d > best[(size_t) w].db) best[(size_t) w] = { hz, d, 0 }; }
    }
    std::sort (best.begin(), best.begin() + nb, [] (const auto& x, const auto& y) { return x.hz < y.hz; });
    for (int j = 0; j < nb; ++j) best[(size_t) j].x = (float) (std::log (best[(size_t) j].hz / 40.0) / std::log (300.0));
    vs.peaks = best; vs.numPeaks = nb;
    // frequenza dominante: il picco più forte
    vs.domHz = 0; vs.domDb = -100;
    for (int j = 0; j < nb; ++j) if (best[(size_t) j].db > vs.domDb) { vs.domDb = best[(size_t) j].db; vs.domHz = best[(size_t) j].hz; }
}

void BSynthesiaEditor::timerCallback()
{
    analyse();
    canvas.advance (1.0f / 60.0f);
    if (canvas.getBounds() != canvasArea()) canvas.setBounds (canvasArea());
    canvas.repaint();
    // rec sync: mentre la DAW suona, un fotogramma ogni ~33 ms (30 fps) renderizzato fuori schermo e passato al thread
    if (recArmed && proc.playing.load())
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastFrameMs >= 33.0)
        {
            lastFrameMs = now;
            const int asp = (int) proc.apvts.getRawParameterValue (BSynIDs::aspect)->load();
            const int ew = asp == 2 ? 720 : 1280, eh = asp == 2 ? 1280 : (asp == 1 ? 720 : juce::roundToInt (1280.0f * (float) canvas.getHeight() / (float) juce::jmax (1, canvas.getWidth())));
            juce::Image img (juce::Image::ARGB, ew, eh, true);
            { juce::Graphics ig (img); canvas.render (ig, juce::Rectangle<float> (0, 0, (float) ew, (float) eh), true); }
            writer.push (img, juce::String::formatted ("frame_%06d_t%09.0fms.png", frameCounter++, proc.timeMs.load()));
        }
        repaint (0, 78, getWidth(), 60);
    }
}
