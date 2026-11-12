#include "PluginProcessor.h"
#include "PluginEditor.h"

// ============================================================================ builder (thread a parte)
class BBlendProcessor::Builder final : public juce::Thread
{
public:
    explicit Builder (BBlendProcessor& p) : juce::Thread ("b-blend IR"), owner (p) { startThread(); }
    ~Builder() override { signalThreadShouldExit(); notify(); stopThread (2000); }
    void run() override
    {
        while (! threadShouldExit())
        {
            bool worked = false;
            for (int s = 0; s < 4; ++s)   // file scelti dall'interfaccia: lettura e decodifica qui, mai sul thread dei messaggi
            {
                juce::File f;
                { const juce::ScopedLock sl (owner.slotLock); std::swap (f, owner.pendingFiles[(size_t) s]); }
                if (f != juce::File()) { owner.loadIrFile (s, f); owner.loading[(size_t) s].store (false); worked = true; }
            }
            const int req = owner.buildRequests.load();
            if (req != owner.buildsDone.load()) { owner.build(); owner.buildsDone.store (req); worked = true; }
            if (! worked) wait (-1);
        }
    }
private:
    BBlendProcessor& owner;
};

namespace
{
    // Ritardo (campioni, con frazione) di x rispetto a ref, dal picco della cross-correlazione via FFT.
    double findDelay (const std::vector<float>& ref, const std::vector<float>& x)
    {
        const int order = juce::jmax (8, (int) std::ceil (std::log2 ((double) (ref.size() + x.size()))));
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> a ((size_t) (2 * N), 0.0f), b ((size_t) (2 * N), 0.0f);
        std::copy (ref.begin(), ref.end(), a.begin());
        std::copy (x.begin(), x.end(), b.begin());
        fft.performRealOnlyForwardTransform (a.data());
        fft.performRealOnlyForwardTransform (b.data());
        for (int k = 0; k <= N / 2; ++k)   // R = conj(A) * B: picco al ritardo di x rispetto a ref
        {
            const float ar = a[(size_t) (2 * k)], ai = a[(size_t) (2 * k + 1)], br = b[(size_t) (2 * k)], bi = b[(size_t) (2 * k + 1)];
            a[(size_t) (2 * k)] = ar * br + ai * bi;
            a[(size_t) (2 * k + 1)] = ar * bi - ai * br;
        }
        fft.performRealOnlyInverseTransform (a.data());

        // 1) attacco di ciascun IR (primo campione sopra il 10 % del picco): cabinet con timbri molto diversi
        //    hanno correlazioni ambigue, l'attacco no
        auto onset = [] (const std::vector<float>& v)
        {
            float mx = 0; for (float s : v) mx = juce::jmax (mx, std::abs (s));
            for (size_t i = 0; i < v.size(); ++i) if (std::abs (v[i]) >= 0.1f * mx) return (int) i;
            return 0;
        };
        const int byOnset = onset (x) - onset (ref);
        // 2) la correlazione affina, ma solo vicino all'attacco (±0,25 ms) e se gli IR si somigliano
        double ea = 0, eb = 0; for (float v : ref) ea += (double) v * v; for (float v : x) eb += (double) v * v;
        const int win = juce::jmax (2, (int) (0.00025 * 48000.0));
        int best = (byOnset + N) % N;
        for (int k = byOnset - win; k <= byOnset + win; ++k)
            if (std::abs (a[(size_t) ((k + N) % N)]) > std::abs (a[(size_t) best])) best = (k + N) % N;
        const double similarity = std::abs (a[(size_t) best]) / std::sqrt (ea * eb + 1e-30);   // 1 = identici
        if (similarity < 0.6) return (double) byOnset;
        auto at = [&] (int i) { return std::abs (a[(size_t) ((i + N) % N)]); };
        const double y0 = at (best - 1), y1 = at (best), y2 = at (best + 1);
        const double den = y0 - 2 * y1 + y2;
        double lag = best + (std::abs (den) > 1e-20 ? 0.5 * (y0 - y2) / den : 0.0);   // interpolazione parabolica
        if (lag > N / 2) lag -= N;
        return lag;
    }

    // Sposta x di -delay campioni (anche frazionari) con una rampa di fase lineare nel dominio FFT.
    void advance (std::vector<float>& x, double delay)
    {
        const size_t len = x.size();
        const int order = juce::jmax (8, (int) std::ceil (std::log2 ((double) len * 2.0)));
        const int N = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> d ((size_t) (2 * N), 0.0f);
        std::copy (x.begin(), x.end(), d.begin());
        fft.performRealOnlyForwardTransform (d.data());
        for (int k = 0; k <= N / 2; ++k)
        {
            const double ph = juce::MathConstants<double>::twoPi * k * delay / N;
            const float c = (float) std::cos (ph), s = (float) std::sin (ph);
            const float re = d[(size_t) (2 * k)], im = d[(size_t) (2 * k + 1)];
            d[(size_t) (2 * k)] = re * c - im * s;
            d[(size_t) (2 * k + 1)] = re * s + im * c;
        }
        fft.performRealOnlyInverseTransform (d.data());
        std::copy (d.begin(), d.begin() + (long) len, x.begin());
    }
}

// ============================================================================ processore
BBlendProcessor::BBlendProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BBlend", createLayout())
{
    pX = apvts.getRawParameterValue (BBlendIDs::x); pY = apvts.getRawParameterValue (BBlendIDs::y);
    pThump = apvts.getRawParameterValue (BBlendIDs::thump); pAir = apvts.getRawParameterValue (BBlendIDs::air);
    pLo = apvts.getRawParameterValue (BBlendIDs::locut); pHi = apvts.getRawParameterValue (BBlendIDs::hicut);
    builder = std::make_unique<Builder> (*this);
    startTimerHz (30);
}

BBlendProcessor::~BBlendProcessor() { stopTimer(); builder.reset(); }

juce::AudioProcessorValueTreeState::ParameterLayout BBlendProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BBlendIDs::x, 1 }, "X", NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BBlendIDs::y, 1 }, "Y", NormalisableRange<float> (0.0f, 1.0f), 0.5f));
    auto pct = [] (const char* id, const char* name)
    { return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f,
                                                    AudioParameterFloatAttributes().withLabel ("%")); };
    l.add (pct (BBlendIDs::thump, "Thump"));
    l.add (pct (BBlendIDs::air, "Air"));
    NormalisableRange<float> lo (0.0f, 400.0f, 1.0f); lo.setSkewForCentre (100.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BBlendIDs::locut, 1 }, "Low cut", lo, 0.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    NormalisableRange<float> hi (2000.0f, 20000.0f, 10.0f); hi.setSkewForCentre (7000.0f);
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BBlendIDs::hicut, 1 }, "High cut", hi, 20000.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    return l;
}

bool BBlendProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in == juce::AudioChannelSet::mono()) return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return in == juce::AudioChannelSet::stereo() && out == in;
}

void BBlendProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    fs.store (sampleRate);
    convolution.prepare ({ sampleRate, (juce::uint32) samplesPerBlock, 2 });
    convolution.reset();
    for (auto& c : ch) c = {};
    lowBand = {}; lowBand.set (sp::Bq::BP, sampleRate, 95.0, 1.0);
    eAtk = sp::coefMs (5.0, sampleRate); eRel = sp::coefMs (80.0, sampleRate);
    env = 0; appliedThump = 100; lastAir = -1;

    loCut = {}; loCut.hp = true; hiCut = {};
    loCut.logHz = std::log2 (juce::jmax (20.0f, pLo->load())); hiCut.logHz = std::log2 (pHi->load());
    envSteps.assign ((size_t) (samplesPerBlock / 32 + 2), 0.0f);
    builtSlotsVersion.store (-1);   // IR da ricampionare al nuovo sample rate
    requestBuild();
}

void BBlendProcessor::requestBuild() { ++buildRequests; builder->notify(); }

void BBlendProcessor::timerCallback()
{
    const float x = pX->load(), y = pY->load();
    if (std::abs (x - lastX) > 0.002f || std::abs (y - lastY) > 0.002f) { lastX = x; lastY = y; requestBuild(); }
}

void BBlendProcessor::loadIr (int slot, const juce::AudioBuffer<float>& data, double rate, const juce::String& name)
{
    {
        const juce::ScopedLock sl (slotLock);
        auto& s = slots[(size_t) juce::jlimit (0, 3, slot)];
        const int len = juce::jmin (data.getNumSamples(), (int) (kMaxIrSeconds * rate));
        s.data.setSize (1, len);
        s.data.clear();
        for (int c = 0; c < data.getNumChannels(); ++c) s.data.addFrom (0, 0, data, c, 0, len, 1.0f / (float) data.getNumChannels());   // mono
        s.rate = rate;
        s.name = name;
    }
    ++slotsVersion;
    requestBuild();
}

bool BBlendProcessor::loadIrFile (int slot, const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (kMaxIrSeconds * r->sampleRate));
    juce::AudioBuffer<float> b ((int) r->numChannels, len);
    r->read (&b, 0, len, 0, true, true);
    loadIr (slot, b, r->sampleRate, f.getFileNameWithoutExtension());
    { const juce::ScopedLock sl (slotLock); slots[(size_t) juce::jlimit (0, 3, slot)].path = f.getFullPathName(); }
    return true;
}

juce::File BBlendProcessor::getSlotFile (int slot) const
{
    slot = juce::jlimit (0, 3, slot);
    if (slotFiles[(size_t) slot] != juce::File()) return slotFiles[(size_t) slot];
    const juce::ScopedLock sl (slotLock);
    const auto& s = slots[(size_t) slot];
    return s.data.getNumSamples() > 0 && juce::File::isAbsolutePath (s.path) ? juce::File (s.path) : juce::File();
}

namespace
{
    juce::File dirFrom (const juce::String& path)
    {
        if (! juce::File::isAbsolutePath (path)) return {};
        const juce::File d (path);
        return d.isDirectory() ? d : juce::File();
    }
}

juce::File BBlendProcessor::getLastFolder() const
{
    const juce::ScopedLock sl (slotLock);
    return dirFrom (lastFolder);
}

juce::File BBlendProcessor::getBrowseFolder (int slot) const
{
    slot = juce::jlimit (0, 3, slot);
    const auto f = getSlotFile (slot);
    juce::String chosen;
    { const juce::ScopedLock sl (slotLock); chosen = slots[(size_t) slot].folder; }
    if (const auto d = dirFrom (chosen); d != juce::File() && (f == juce::File() || f.isAChildOf (d))) return d;
    if (f != juce::File() && f.getParentDirectory().isDirectory()) return f.getParentDirectory();
    return getLastFolder();
}

void BBlendProcessor::setBrowseFolder (int slot, const juce::File& dir)
{
    const juce::ScopedLock sl (slotLock);
    slots[(size_t) juce::jlimit (0, 3, slot)].folder = dir.getFullPathName();
    lastFolder = dir.getFullPathName();
}

int BBlendProcessor::slotUsing (const juce::File& f) const
{
    if (f == juce::File()) return -1;
    for (int i = 0; i < 4; ++i) if (getSlotFile (i) == f) return i;
    return -1;
}

bool BBlendProcessor::loadIrFileAsync (int slot, const juce::File& f)
{
    slot = juce::jlimit (0, 3, slot);
    const int used = slotUsing (f);
    if (used >= 0 && used != slot) return false;   // lo stesso IR non va in due slot
    { const juce::ScopedLock sl (slotLock); pendingFiles[(size_t) slot] = f; }
    loading[(size_t) slot].store (true);
    slotFiles[(size_t) slot] = f;
    builder->notify();
    return true;
}

void BBlendProcessor::setSoloSlot (int slot)
{
    if (soloSlot.exchange (slot) != slot) requestBuild();
}

double BBlendProcessor::getSlotRate (int slot) const
{
    const juce::ScopedLock sl (slotLock);
    return slots[(size_t) slot].data.getNumSamples() > 0 ? slots[(size_t) slot].rate : 0.0;
}

void BBlendProcessor::removeIr (int slot)
{
    {
        const juce::ScopedLock sl (slotLock);
        auto& s = slots[(size_t) juce::jlimit (0, 3, slot)];
        s.data.setSize (1, 0); s.rate = 0; s.name = {}; s.path = {};
    }
    slotFiles[(size_t) juce::jlimit (0, 3, slot)] = juce::File();
    ++slotsVersion;
    requestBuild();
}

int BBlendProcessor::shapeVertices (int count, std::array<juce::Point<float>, 4>& v)
{
    switch (count)
    {
        case 1: v[0] = { 0.5f, 0.5f }; break;
        case 2: v[0] = { 0.0f, 0.5f }; v[1] = { 1.0f, 0.5f }; break;
        case 3:   // equilatero di lato 1, centrato nel quadrato (altezza sqrt(3)/2)
        {
            const float h = 0.8660254f;
            v[0] = { 0.5f, 0.5f + h * 0.5f }; v[1] = { 0.0f, 0.5f - h * 0.5f }; v[2] = { 1.0f, 0.5f - h * 0.5f }; break;
        }
        case 4: v[0] = { 0.0f, 1.0f }; v[1] = { 1.0f, 1.0f }; v[2] = { 0.0f, 0.0f }; v[3] = { 1.0f, 0.0f }; break;
        default: break;
    }
    return count;
}

namespace
{
    // coordinate baricentriche di p nel triangolo abc, riportate dentro (nessun peso negativo)
    std::array<float, 3> barycentric (juce::Point<float> p, juce::Point<float> a, juce::Point<float> b, juce::Point<float> c)
    {
        const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        float l1 = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
        float l2 = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
        float l3 = 1.0f - l1 - l2;
        l1 = juce::jmax (0.0f, l1); l2 = juce::jmax (0.0f, l2); l3 = juce::jmax (0.0f, l3);
        const float s = l1 + l2 + l3;
        return { l1 / s, l2 / s, l3 / s };
    }
}

void BBlendProcessor::weightsFor (float x, float y, int count, std::array<float, 4>& w)
{
    w.fill (0.0f);
    const float h = juce::MathConstants<float>::halfPi;
    if (count == 1) w[0] = 1.0f;
    else if (count == 2) { w[0] = std::cos (x * h); w[1] = std::sin (x * h); }
    else if (count == 3)
    {
        std::array<juce::Point<float>, 4> v; shapeVertices (3, v);
        const auto b = barycentric ({ x, y }, v[0], v[1], v[2]);
        for (int i = 0; i < 3; ++i) w[(size_t) i] = std::sqrt (b[(size_t) i]);   // potenza costante
    }
    else if (count == 4)
    {
        w[0] = std::cos (x * h) * std::sin (y * h); w[1] = std::sin (x * h) * std::sin (y * h);
        w[2] = std::cos (x * h) * std::cos (y * h); w[3] = std::sin (x * h) * std::cos (y * h);
    }
}

juce::Point<float> BBlendProcessor::constrain (float x, float y, int count)
{
    if (count <= 1) return { 0.5f, 0.5f };
    if (count == 2) return { x, 0.5f };
    if (count == 3)
    {
        std::array<juce::Point<float>, 4> v; shapeVertices (3, v);
        const auto b = barycentric ({ x, y }, v[0], v[1], v[2]);
        return v[0] * b[0] + v[1] * b[1] + v[2] * b[2];
    }
    return { x, y };
}

juce::Point<float> BBlendProcessor::centreOf (int count)
{
    if (count != 3) return { 0.5f, 0.5f };
    std::array<juce::Point<float>, 4> v; shapeVertices (3, v);
    return (v[0] + v[1] + v[2]) / 3.0f;   // baricentro: i tre IR in parti uguali
}

juce::String BBlendProcessor::getSlotName (int slot) const
{
    const juce::ScopedLock sl (slotLock);
    return slots[(size_t) slot].name;
}

bool BBlendProcessor::waitForBuild (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        bool busy = false; for (auto& l : loading) busy = busy || l.load();
        if (! busy && buildsDone.load() == buildRequests.load()) return true;
        juce::Thread::sleep (2);
    }
    return false;
}

// Thread del builder: ricampiona e allinea gli IR (solo se sono cambiati), poi mescola e carica.
void BBlendProcessor::build()
{
    const double rate = fs.load();
    const int sv = slotsVersion.load();
    if (sv != builtSlotsVersion.load())
    {
        std::array<Slot, 4> copy;
        { const juce::ScopedLock sl (slotLock); for (size_t i = 0; i < 4; ++i) { copy[i].data.makeCopyOf (slots[i].data); copy[i].rate = slots[i].rate; } }
        int ref = -1;
        for (size_t i = 0; i < 4; ++i)
        {
            aligned[i].clear();
            if (copy[i].data.getNumSamples() == 0) continue;
            // ricampionamento al sample rate di lavoro
            const double ratio = copy[i].rate / rate;
            const int outLen = (int) std::ceil (copy[i].data.getNumSamples() / ratio);
            if (std::abs (ratio - 1.0) < 1e-9)
                aligned[i].assign (copy[i].data.getReadPointer (0), copy[i].data.getReadPointer (0) + copy[i].data.getNumSamples());
            else
            {
                juce::LagrangeInterpolator li;
                const int skip = juce::roundToInt (li.getBaseLatency() / ratio);   // l'interpolatore ritarda: si recupera
                std::vector<float> tmp ((size_t) (outLen + skip), 0.0f);
                li.process (ratio, copy[i].data.getReadPointer (0), tmp.data(), outLen + skip, copy[i].data.getNumSamples(), 0);
                aligned[i].assign (tmp.begin() + skip, tmp.end());
            }
            // energia unitaria: cabinet diversi allo stesso volume, il blend non salta di livello
            double e = 0; for (float v : aligned[i]) e += (double) v * v;
            if (e > 1e-20) { const float g = (float) (1.0 / std::sqrt (e)); for (auto& v : aligned[i]) v *= g; }
            if (ref < 0) { ref = (int) i; alignShift[i].store (0.0f); continue; }
            const double d = findDelay (aligned[(size_t) ref], aligned[i]);
            advance (aligned[i], d);   // allineato al riferimento, anche sotto il campione
            alignShift[i].store ((float) d);
        }
        builtSlotsVersion.store (sv);
    }

    // pesi a potenza costante secondo la forma: 1 IR = solo quello, 2 = linea, 3 = triangolo, 4 = quadrato
    // (vertici nell'ordine degli slot caricati)
    std::array<int, 4> loaded {}; int count = 0; size_t len = 0;
    for (int i = 0; i < 4; ++i) if (! aligned[(size_t) i].empty()) { loaded[(size_t) count++] = i; len = juce::jmax (len, aligned[(size_t) i].size()); }
    if (count == 0) { hasIr.store (false); ++responseVersion; return; }
    std::array<float, 4> w;
    weightsFor (pX->load(), pY->load(), count, w);
    // mentre si sceglie un IR dalla cartella si sente solo quello (se lo slot ne ha già uno)
    const int solo = soloSlot.load();
    if (juce::isPositiveAndBelow (solo, 4) && ! aligned[(size_t) solo].empty())
        for (int k = 0; k < count; ++k) w[(size_t) k] = loaded[(size_t) k] == solo ? 1.0f : 0.0f;
    juce::AudioBuffer<float> mix (1, (int) len);
    mix.clear();
    for (int k = 0; k < count; ++k)
    {
        const auto& ir = aligned[(size_t) loaded[(size_t) k]];
        if (w[(size_t) k] > 0.0f) mix.addFrom (0, 0, ir.data(), (int) ir.size(), w[(size_t) k]);
    }

    // volume d'uscita = volume d'ingresso: l'IR composito si normalizza sul guadagno medio che ha per un segnale di
    // chitarra (spettro rosa, 1/f, tra 80 Hz e 6 kHz), non sull'energia totale. Un cabinet ha la sua risonanza proprio
    // lì: normalizzato per energia suonerebbe diversi dB più forte (e farebbe clippare tutto quello che segue).
    {
        const int order = 15, N = 1 << order;
        std::vector<float> d ((size_t) (2 * N), 0.0f);
        std::copy (mix.getReadPointer (0), mix.getReadPointer (0) + juce::jmin ((int) len, N), d.begin());
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (d.data(), true);
        double num = 0, den = 0;
        for (int k = 1; k < N / 2; ++k)
        {
            const double hz = k * rate / N;
            if (hz < 80.0 || hz > 6000.0) continue;
            const double wt = 1.0 / hz;
            num += wt * (double) d[(size_t) k] * d[(size_t) k]; den += wt;
        }
        if (num > 1e-20) mix.applyGain ((float) std::sqrt (den / num));
    }

    // risposta per il grafico
    {
        const int order = 14, N = 1 << order;
        std::vector<float> d ((size_t) (2 * N), 0.0f);
        std::copy (mix.getReadPointer (0), mix.getReadPointer (0) + juce::jmin ((int) len, N), d.begin());
        juce::dsp::FFT (order).performFrequencyOnlyForwardTransform (d.data(), true);
        float mx = 1e-9f; for (int k = 0; k <= N / 2; ++k) mx = juce::jmax (mx, d[(size_t) k]);
        std::vector<float> db ((size_t) (N / 2 + 1));
        for (int k = 0; k <= N / 2; ++k) db[(size_t) k] = 20.0f * std::log10 (d[(size_t) k] / mx + 1e-9f);
        const juce::SpinLock::ScopedLockType l (respLock);
        respDb = std::move (db); respBinHz = rate / N;
    }
    convolution.loadImpulseResponse (std::move (mix), rate, juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    hasIr.store (true);
    ++responseVersion;
}

float BBlendProcessor::irResponseDb (double hz) const
{
    const juce::SpinLock::ScopedLockType l (respLock);
    if (respDb.size() < 2) return 0.0f;
    const double b = juce::jlimit (0.0, (double) respDb.size() - 2.0, hz / respBinHz);
    const int i = (int) b; const float t = (float) (b - i);
    return respDb[(size_t) i] + t * (respDb[(size_t) i + 1] - respDb[(size_t) i]);
}

float BBlendProcessor::filtersDb (double hz) const
{
    const double rate = fs.load();
    double mag = 1.0;
    sp::Bq b;
    b.set (sp::Bq::Peak, rate, 98.0, 2.0, thumpDb.load()); mag *= b.magnitude (hz, rate);
    if (pAir->load() > 0.0f) { b.set (sp::Bq::HighShelf, rate, 7000.0, 0.7071, 12.0 * pAir->load() / 100.0); mag *= b.magnitude (hz, rate); }
    if (pLo->load() >= 1.0f) { b.set (sp::Bq::HP, rate, pLo->load(), 0.7071); mag *= b.magnitude (hz, rate); }
    if (pHi->load() < 19990.0f) { b.set (sp::Bq::LP, rate, pHi->load(), 0.7071); mag *= b.magnitude (hz, rate); }
    return (float) (20.0 * std::log10 (juce::jmax (mag, 1e-6)));
}

void BBlendProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    const int n = buffer.getNumSamples();
    const double rate = fs.load();
    if (numIn == 1 && numOut == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono -> stereo: stesso cabinet sui due lati
    const int numCh = juce::jmin (2, numOut);
    // nessun IR: uscita muta (resta acceso). Un ampli senza cassa arriverebbe alle orecchie crudo e fortissimo.
    if (! hasIr.load()) { buffer.clear(); return; }
    // ingresso abbassato di 7 dB fissi: dopo un ampli (es. Neural DSP) il livello arriva troppo caldo al cabinet
    for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::multiply (buffer.getWritePointer (c), kInputTrim, n);

    // banda bassa (60-150 Hz) misurata sull'ingresso, prima del cabinet: segue il palm-mute dell'ampli
    const int steps = (n + 31) / 32;
    for (int i = 0; i < n; ++i)
    {
        float s = buffer.getReadPointer (0)[i]; if (numCh > 1) s += buffer.getReadPointer (1)[i];
        const float b = lowBand.process (s / (float) numCh), p = b * b;
        env = p > env ? eAtk * (env - p) + p : eRel * (env - p) + p;
        if ((i & 31) == 31 || i == n - 1) envSteps[(size_t) juce::jmin (i / 32, (int) envSteps.size() - 1)] = env;
    }
    juce::ignoreUnused (steps);

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubsetChannelBlock (0, (size_t) numCh);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (sub));

    const float air = pAir->load(), lo = pLo->load(), hi = pHi->load(), thump = pThump->load() / 100.0f;
    if (std::abs (air - lastAir) > 0.01f) { for (auto& c : ch) c.air.set (sp::Bq::HighShelf, rate, 7000.0, 0.7071, 12.0 * air / 100.0); lastAir = air; }
    float* chans[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i0 = 0; i0 < n; i0 += 32)
    {
        // RMS della banda bassa -> guadagno della campana a 98 Hz (ricalcolato ogni 32 campioni)
        const float e = envSteps[(size_t) juce::jmin (i0 / 32, (int) envSteps.size() - 1)];
        const float g = thump * juce::jmin (12.0f, std::sqrt (e) * 36.0f);   // fino a +12 dB sui colpi forti
        if (std::abs (g - appliedThump) > 0.05f) { for (auto& c : ch) c.thump.set (sp::Bq::Peak, rate, 98.0, 2.0, g); appliedThump = g; thumpDb.store (g); }
        const int i1 = juce::jmin (n, i0 + 32);
        for (int c = 0; c < numCh; ++c)
        {
            auto& st = ch[(size_t) c];
            float* d = chans[c];
            if (air > 0.0f) for (int i = i0; i < i1; ++i) d[i] = st.air.process (st.thump.process (d[i]));
            else            for (int i = i0; i < i1; ++i) d[i] = st.thump.process (d[i]);
        }
    }
    runCut (loCut, juce::jmax (20.0f, lo), lo >= 1.0f, chans, numCh, n, rate);
    runCut (hiCut, hi, hi < 19990.0f, chans, numCh, n, rate);
    for (auto& c : ch) { c.thump.flush(); c.air.flush(); }
    for (auto* sc : { &loCut, &hiCut }) for (auto& f : sc->f) f.flush();
    lowBand.flush();
}

void BBlendProcessor::runCut (SmoothCut& s, float targetHz, bool active, float* const* chans, int numCh, int n, double rate) noexcept
{
    if (! active && s.mix <= 0.0f) return;   // spento e già dissolto: costo zero
    const float tLog = std::log2 (targetHz), mixT = active ? 1.0f : 0.0f;
    const float k = 1.0f - std::exp (-16.0f / (0.03f * (float) rate));   // ~30 ms in ottave, a passi da 16 campioni
    for (int i0 = 0; i0 < n; i0 += 16)
    {
        const float prevMix = s.mix;
        s.logHz += k * (tLog - s.logHz);
        s.mix += k * (mixT - s.mix);
        if (std::abs (s.mix - mixT) < 1.0e-4f) s.mix = mixT;
        for (auto& f : s.f) f.set (rate, std::exp2 ((double) s.logHz), s.q);
        const int i1 = juce::jmin (n, i0 + 16);
        const float dm = (s.mix - prevMix) / (float) (i1 - i0);
        for (int c = 0; c < numCh; ++c)
        {
            auto& f = s.f[(size_t) c];
            float* d = chans[c];
            float m = prevMix;
            for (int i = i0; i < i1; ++i)
            {
                m += dm;
                const float y = s.hp ? f.processHP (d[i]) : f.processLP (d[i]);
                d[i] += m * (y - d[i]);
            }
        }
    }
    if (s.mix <= 0.0f) for (auto& f : s.f) f.reset();   // spento: si riparte puliti dalla dissolvenza
}

// Stato: parametri + i dati degli IR (il progetto resta completo anche senza i file originali).
void BBlendProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto state = apvts.copyState();
    juce::ValueTree irs ("IRS");
    {
        const juce::ScopedLock sl (slotLock);
        for (int i = 0; i < 4; ++i)
        {
            const auto& s = slots[(size_t) i];
            if (s.data.getNumSamples() == 0) continue;
            juce::MemoryBlock mb (s.data.getReadPointer (0), (size_t) s.data.getNumSamples() * sizeof (float));
            irs.appendChild (juce::ValueTree ("IR").setProperty ("slot", i, nullptr).setProperty ("name", s.name, nullptr)
                                                   .setProperty ("rate", s.rate, nullptr).setProperty ("path", s.path, nullptr)
                                                   .setProperty ("folder", s.folder, nullptr)
                                                   .setProperty ("data", mb.toBase64Encoding(), nullptr), nullptr);
        }
        irs.setProperty ("last", lastFolder, nullptr);
    }
    state.removeChild (state.getChildWithName ("IRS"), nullptr);
    state.appendChild (irs, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, d);
}

void BBlendProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    const auto irs = tree.getChildWithName ("IRS");
    tree.removeChild (irs, nullptr);
    apvts.replaceState (tree);
    for (const auto& ir : irs)
    {
        juce::MemoryBlock mb;
        if (! mb.fromBase64Encoding (ir.getProperty ("data").toString())) continue;
        juce::AudioBuffer<float> b (1, (int) (mb.getSize() / sizeof (float)));
        std::memcpy (b.getWritePointer (0), mb.getData(), (size_t) b.getNumSamples() * sizeof (float));
        const int slot = juce::jlimit (0, 3, (int) ir.getProperty ("slot"));
        loadIr (slot, b, (double) ir.getProperty ("rate"), ir.getProperty ("name").toString());
        const juce::ScopedLock sl (slotLock);
        slots[(size_t) slot].path = ir.getProperty ("path").toString();   // per riaprire il browser nella sua cartella
        slots[(size_t) slot].folder = ir.getProperty ("folder").toString();
    }
    const juce::ScopedLock sl (slotLock);
    if (irs.hasProperty ("last")) lastFolder = irs.getProperty ("last").toString();
}

juce::AudioProcessorEditor* BBlendProcessor::createEditor() { return new BBlendEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BBlendProcessor(); }
