#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* ids[12] = { BAmpIDs::channel, BAmpIDs::os, BAmpIDs::gate, BAmpIDs::tight, BAmpIDs::gain, BAmpIDs::bass, BAmpIDs::mid,
                            BAmpIDs::treble, BAmpIDs::presence, BAmpIDs::resonance, BAmpIDs::master, BAmpIDs::level };
    enum { P_CH, P_OS, P_GATE, P_TIGHT, P_GAIN, P_BASS, P_MID, P_TREB, P_PRES, P_RES, P_MASTER, P_LEVEL };
}

BAmpProcessor::BAmpProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "BAmp", createLayout())
{
    for (int i = 0; i < 12; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
    pGateOn = apvts.getRawParameterValue (BAmpIDs::gateOn);
    pBoost = apvts.getRawParameterValue (BAmpIDs::boost);
    pBoostTone = apvts.getRawParameterValue (BAmpIDs::boostTone);
    for (const char* id : { BAmpIDs::tight, BAmpIDs::gain, BAmpIDs::bass, BAmpIDs::mid, BAmpIDs::treble, BAmpIDs::presence, BAmpIDs::resonance,
                            BAmpIDs::master, BAmpIDs::level, BAmpIDs::gate, BAmpIDs::gateOn, BAmpIDs::boost, BAmpIDs::boostTone, BAmpIDs::os })
        headMemory.add (apvts.getParameter (id), [id] (int ch) { return headDefault (id, ch); });
}

// default di ciascuna testata (i default dei parametri sono quelli del crunch, il canale di partenza)
float BAmpProcessor::headDefault (const char* id, int ch)
{
    struct D { const char* id; float v[3]; };
    static const D table[] = {
        { BAmpIDs::tight,     { 0, 0, 0 } },
        { BAmpIDs::gain,      { 10, 50, 30 } },
        { BAmpIDs::bass,      { 45, 50, 55 } },
        { BAmpIDs::mid,       { 60, 50, 45 } },
        { BAmpIDs::treble,    { 55, 60, 60 } },
        { BAmpIDs::presence,  { 35, 40, 45 } },
        { BAmpIDs::resonance, { 35, 40, 55 } },
        { BAmpIDs::master,    { 50, 50, 50 } },
        { BAmpIDs::level,     { 0, 0, 0 } },
        { BAmpIDs::gate,      { -60, -60, -60 } },
        { BAmpIDs::gateOn,    { 0, 0, 0 } },        // noise gate spento di default
        { BAmpIDs::boost,     { 0, 0, 0 } },
        { BAmpIDs::boostTone, { 50, 50, 50 } },
        { BAmpIDs::os,        { 1, 1, 1 } },        // 2x
    };
    for (const auto& d : table) if (juce::String (d.id) == id) return d.v[juce::jlimit (0, 2, ch)];
    return 0.0f;
}

juce::AudioProcessorValueTreeState::ParameterLayout BAmpProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { l.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel (unit))); };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BAmpIDs::channel, 1 }, "Channel", StringArray { "Clean", "Crunch", "Lead" }, 1));
    // off, 2x, 4x (i progetti salvati prima, con 0 = 2x e 1 = 4x, si rimappano in setStateInformation)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BAmpIDs::os, 1 }, "Oversampling", StringArray { "Off", "2x", "4x" }, 1));
    f (BAmpIDs::gate, "Gate", { -90.0f, -20.0f, 0.5f }, -60.0f, "dB");
    l.add (std::make_unique<AudioParameterBool> (ParameterID { BAmpIDs::gateOn, 1 }, "Noise gate", false));
    f (BAmpIDs::tight, "Tight", { 0.0f, 100.0f, 1.0f }, 0.0f, "%");
    f (BAmpIDs::gain, "Gain", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BAmpIDs::bass, "Bass", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BAmpIDs::mid, "Mid", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BAmpIDs::treble, "Treble", { 0.0f, 100.0f, 1.0f }, 60.0f, "%");
    f (BAmpIDs::presence, "Presence", { 0.0f, 100.0f, 1.0f }, 40.0f, "%");
    f (BAmpIDs::resonance, "Resonance", { 0.0f, 100.0f, 1.0f }, 40.0f, "%");
    f (BAmpIDs::master, "Master", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    f (BAmpIDs::level, "Level", { -24.0f, 12.0f, 0.1f }, 0.0f, "dB");
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BAmpIDs::boost, 1 }, "Boost", StringArray { "Off", "Vol", "Vol + EQ" }, 0));
    f (BAmpIDs::boostTone, "Boost tone", { 0.0f, 100.0f, 1.0f }, 50.0f, "%");
    return l;
}

bool BAmpProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BAmpProcessor::prepareToPlay (double rate, int block)
{
    fs = rate;
    os2.reset(); os4.reset();
    os2.initProcessing ((size_t) block); os4.initProcessing ((size_t) block);
    mono.setSize (1, block);
    inHp = {}; inHp.set (sp::Bq::HP, fs, 35.0, 0.7071);
    notch50 = {}; notch50.set (sp::Bq::BP, fs, 50.0, 40.0); notch100 = {}; notch100.set (sp::Bq::BP, fs, 100.0, 40.0);
    tunerLp = {}; tunerLp.set (sp::Bq::LP, fs, 500.0, 0.7071);
    diLp = {}; diLp.set (sp::Bq::LP, fs, 6500.0, 0.7071); tunerDecim = 0; tunerFifo.reset();
    det.prepare (fs); hissLp.set (fs, 12000.0); hissLp2.set (fs, 12000.0); hissCut = 12000;
    tone.reset(); toneCtl = 0; lastB = lastM = lastT = -1; lastBright = -1;
    pclip.reset(); sagEnv = 0; sagA = sp::coefMs (10.0, fs); sagR = sp::coefMs (150.0, fs);
    resPeak = {}; presShelf = {}; safeLp = {}; safeLp.set (sp::Bq::LP, fs, 11500.0, 0.7071); lastRes = lastPres = -1;
    masterSm.reset (fs, 0.02); levelSm.reset (fs, 0.02);
    masterSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (-30.0f + 36.0f * p[P_MASTER]->load() / 100.0f));
    levelSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p[P_LEVEL]->load()));
    // boost: +6 dB; il livello alto aggiunge un passa-alto a 90 Hz (bassi più stretti nel preamp), +4,5 dB a 1,6 kHz
    // (articolazione e leggibilità della nota), -3 dB sopra i 5,5 kHz (niente fizz) e un drive leggerissimo
    boostGainSm.reset (fs, 0.03); boostEqSm.reset (fs, 0.03);
    potSm.reset (fs, 0.02); potSm.setCurrentAndTargetValue (potFade (p[P_GAIN]->load() / 100.0f));
    { const int b = (int) pBoost->load(); boostGainSm.setCurrentAndTargetValue (b > 0 ? 2.0f : 1.0f); boostEqSm.setCurrentAndTargetValue (b > 1 ? 1.0f : 0.0f); }
    boostLowCut.set (fs, 90.0); boostLowCut.reset();
    boostArt = {}; boostFizz = {}; lastBoostTone = -1.0f;
    setBoostTone (pBoostTone->load() / 100.0f);
    boostShaper.k = 1.25f; boostPrev = 0;
    cfDc.set (fs, 8.0); cfDc.reset(); paDc.set (fs, 8.0); paDc.reset(); otLow.set (fs, 110.0); otLow.reset();
    rampStep = 1.0f / (0.0075f * (float) fs); chanRamp = 1.0f; pendingChannel = -1;
    curChannel = -1; lastTight = -1; lastOs = -1; muteG = 1.0f;
    const int ch = juce::jlimit (0, 2, (int) p[P_CH]->load());
    configureChannel (ch, p[P_TIGHT]->load() / 100.0f);
    setLatencySamples ((int) std::ceil (os4.getLatencyInSamples()));   // 4x ha la latenza più alta; 2x viene compensato al suo valore
}

int BAmpProcessor::osFromParam (float v) noexcept { const int i = juce::roundToInt (v); return i == 0 ? 1 : i == 2 ? 4 : 2; }

float BAmpProcessor::osLatency() const noexcept { return osFactor == 4 ? (float) os4.getLatencyInSamples() : osFactor == 2 ? (float) os2.getLatencyInSamples() : 0.0f; }

// tono del boost vol + eq: 0 = scuro (articolazione a 1 kHz, -8 dB sopra 5,5 kHz), 50 % = il boost di sempre
// (1,6 kHz e -3 dB), 100 % = brillante (2,4 kHz e +2 dB)
void BAmpProcessor::setBoostTone (float t) noexcept
{
    if (std::abs (t - lastBoostTone) < 0.002f) return;
    lastBoostTone = t;
    boostArt.set (sp::Bq::Peak, fs, 1600.0 * std::exp2 (1.2 * (t - 0.5)), 0.8, 4.5);
    boostFizz.set (sp::Bq::HighShelf, fs, 5500.0, 0.7071, -3.0 + 10.0 * (t - 0.5));
}

float BAmpProcessor::stageDrive (int ch, int k, float g) noexcept
{
    // drive per stadio. Clean: lineare, molto headroom. Crunch e lead: in dB, come un potenziometro logaritmico
    // (ogni 10 % di gain aggiunge gli stessi dB), con i guadagni di un preamp high gain a triodi: ogni stadio
    // 12AX7 dà 30-35 dB, e tra uno stadio e l'altro un partitore ne toglie una parte.
    //   lead:   5 stadi, 64 dB in tutto al 30 %, 71 dB al 50 %, 88 dB al 100 % (un muro già al 30 %); il 3° stadio è
    //           il "cold clipper" (polarizzato freddo, clipping asimmetrico), con un po' meno guadagno degli altri
    //   crunch: 4 stadi, curva che accelera in alto: 18 dB a 0, 34 dB a metà (crunch classico), 66 dB al massimo
    //           (abbastanza per il djent)
    const auto db = [] (float d) { return std::pow (10.0f, d / 20.0f); };
    switch (ch)
    {
        case Clean:  return 0.6f + 1.6f * g;
        case Crunch: return db (4.5f + 4.0f * g + 8.0f * g * g);
        default:     return k == 2 ? db (10.0f + 6.0f * g) : db (11.0f + 7.0f * g);
    }
}

void BAmpProcessor::configureChannel (int ch, float tight) noexcept
{
    const double osr = fs * osFactor;
    const bool changed = ch != curChannel || lastOs != osFactor;
    curChannel = ch; lastTight = tight; lastOs = osFactor;
    useBoost = ch == Lead; useBright = ch == Clean;
    // tone stack con i valori dei circuiti di riferimento:
    //   crunch = TMB britannico classico (220k / 1M / 22k / 33k, 470p / 22n / 22n)
    //   lead   = TMB high gain americano  (250k / 250k / 25k / 47k, 250p / 47n / 22n)
    //   clean  = non un TMB "a cucchiaio" ma un passivo a medie piene -> EQ dedicato (cleanEq)
    if (ch == Lead) { tone.R1 = 250e3; tone.R2 = 250e3; tone.R3 = 25e3; tone.R4 = 47e3; tone.C1 = 250e-12; tone.C2 = 47e-9; tone.C3 = 22e-9; }
    else            { tone.R1 = 220e3; tone.R2 = 1e6;   tone.R3 = 22e3; tone.R4 = 33e3; tone.C1 = 470e-12; tone.C2 = 22e-9; tone.C3 = 22e-9; }
    // finale: EL34 (clean, crunch) = morso sulle medio-alte e rottura prima; 6L6 (lead) = basse più
    // ferme, più headroom, alti vetrosi
    if (ch == Lead) tubeEq.set (sp::Bq::LowShelf, fs, 110.0, 0.7071, 1.5);   // 6L6: fondo pieno e fermo
    else            tubeEq.set (sp::Bq::Peak, fs, 1600.0, 0.8, 1.5);
    // voicing del lead: medie basse scavate (420 Hz) e spinta sulle medio-alte dove sta l'aggressività (2,2 kHz)
    if (ch == Lead) voiceEq.set (sp::Bq::Peak, fs, 420.0, 0.9, -2.5);
    else            voiceEq.set (sp::Bq::Peak, fs, 420.0, 0.9, 0.0);
    tubeHead = ch == Lead ? 1.15f : 1.0f;
    // push-pull: il bilanciamento non è mai perfetto -> un po' di 2ª armonica quando il finale satura (EL34 più dello 6L6)
    paAsym = ch == Lead ? 0.04f : (ch == Clean ? 0.05f : 0.08f);
    lastRes = lastPres = -1;
    lastB = lastM = lastT = -1;
    for (int k = 0; k < 5; ++k)
    {
        auto& s = stages[(size_t) k];
        double hp = 30.0;
        if (ch == Clean) hp = k == 0 ? 30.0 : 60.0;
        else if (ch == Crunch) hp = k == 0 ? 40.0 : 55.0 + 125.0 * tight;              // tight: taglio inter-stadio 55 -> 180 Hz
        else hp = k == 0 ? 35.0 : (k == 2 ? 220.0 : 45.0 + 70.0 * tight);               // lead: 3° stadio "cold clipper" HP 220
        if (changed) s.prepare (osr, hp); else s.setHp (hp);
        s.sh.k = ch == Clean ? 1.25f : (ch == Lead && k == 2 ? 2.6f : 1.6f);
        s.bias = ch == Lead && k == 2 ? -0.35f : (ch == Clean ? 0.0f : -0.08f);
        s.kBias = ch == Clean ? 0.15f : (ch == Lead && k == 2 ? 0.45f : 0.3f);
    }
    // lead: boost in ingresso (HP progressivo fino a 240 Hz + spinta a 1,45 kHz Q 1,2)
    boostHp.set (fs, 40.0 + 140.0 * tight);   // passa-alto del 1° ordine, come il condensatore d'ingresso di un overdrive
    boostMid.set (sp::Bq::Peak, fs, 1800.0, 0.9, 3.0 + 6.0 * tight);
    // crunch: picco a 750 Hz con tight alto; clean: bright/punch pre-gain
    brightShelf.set (ch == Crunch ? sp::Bq::Peak : sp::Bq::HighShelf, fs, ch == Crunch ? 750.0 : 2500.0, ch == Crunch ? 1.0 : 0.7071,
                     ch == Crunch ? 4.0 * juce::jmax (0.0f, tight - 0.6f) / 0.4f : 6.0 * (tight - 0.5f));
    // anti-fruscio d'ingresso: serve dove il gain è alto; sul clean resta tutta la brillantezza
    diLp.set (sp::Bq::LP, fs, ch == Clean ? 16000.0 : (ch == Crunch ? 8000.0 : 6500.0), 0.7071);
    if (changed) cfDc.set (osr, 8.0);
    if (changed) { voiceEq.reset(); cfDc.reset(); boostHp.reset(); boostMid.reset(); brightShelf.reset(); tone.reset(); brightCap.reset(); tubeEq.reset(); for (auto& b : cleanEq) b.reset(); }
}

void BAmpProcessor::resetChannelState() noexcept
{
    for (auto& s : stages) s.reset();
    boostHp.reset(); boostMid.reset(); brightShelf.reset(); tone.reset(); pclip.reset(); hissLp.reset(); hissLp2.reset();
    os2.reset(); os4.reset();
}

float BAmpProcessor::toneStackDb (double hz) const { return (float) (20.0 * std::log10 (juce::jmax (1.0e-6, tone.magnitude (hz, fs)))); }

void BAmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels(), n = buffer.getNumSamples();
    const int ch = juce::jlimit (0, 2, (int) p[P_CH]->load());
    const int osWant = osFromParam (p[P_OS]->load());
    const float gateDb = p[P_GATE]->load(), tight = p[P_TIGHT]->load() / 100.0f, gain01 = p[P_GAIN]->load() / 100.0f;
    const float bass = p[P_BASS]->load() / 100.0f, mid = p[P_MID]->load() / 100.0f, treb = p[P_TREB]->load() / 100.0f;
    const float pres = p[P_PRES]->load() / 100.0f, res = p[P_RES]->load() / 100.0f;
    masterSm.setTargetValue (juce::Decibels::decibelsToGain (-30.0f + 36.0f * p[P_MASTER]->load() / 100.0f));
    levelSm.setTargetValue (juce::Decibels::decibelsToGain (p[P_LEVEL]->load()));
    setBoostTone (pBoostTone->load() / 100.0f);

    rampHold = false;   // nuovo blocco: già calcolato con il canale nuovo, la rampa può risalire
    // cambio canale / oversampling: rampa 15 ms (giù, reset degli stati, su)
    if ((ch != curChannel || osWant != osFactor) && pendingChannel < 0) pendingChannel = ch;
    if (std::abs (tight - lastTight) > 0.002f && pendingChannel < 0) configureChannel (ch, tight);
    const bool gateOn = pGateOn->load() > 0.5f;
    const bool hum = gateOn && gateDb > -85.0f;   // il notch del ronzio fa parte del sistema anti-rumore
    det.setThreshold (gateDb);
    for (int k = 0; k < kStages[curChannel]; ++k) stages[(size_t) k].drive = stageDrive (curChannel, k, gain01);
    // resonance: nel lead è più larga e più profonda (fino a +14 dB a 82 Hz, il "thump" di una 4x12 chiusa a volume)
    if (std::abs (res - lastRes) > 0.002f)
    {
        if (curChannel == Lead) resPeak.set (sp::Bq::Peak, fs, 82.0, 1.3, 14.0 * res);
        else                    resPeak.set (sp::Bq::Peak, fs, 96.0, 2.4, 9.0 * res);
        lastRes = res;
    }
    if (std::abs (pres - lastPres) > 0.002f) { presShelf.set (sp::Bq::HighShelf, fs, curChannel == Lead ? 3200.0 : 2800.0, 0.7071, (curChannel == Lead ? 10.0 : 8.0) * pres); lastPres = pres; }
    {
        const int b = (int) pBoost->load();
        boostGainSm.setTargetValue (b > 0 ? 2.0f : 1.0f); boostEqSm.setTargetValue (b > 1 ? 1.0f : 0.0f);
    }

    // ingresso mono (DI); tap del tuner al primo campione assoluto
    float* x = mono.getWritePointer (0);
    const float* in0 = buffer.getReadPointer (0);
    const float* in1 = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    static thread_local std::array<float, 4096> gateG, snrG;
    for (int i = 0; i < n; ++i)
    {
        const float di = in1 != nullptr ? 0.5f * (in0[i] + in1[i]) : in0[i];
        const float tl = tunerLp.process (di);
        if (++tunerDecim >= 8)
        {
            tunerDecim = 0;
            int s1, n1, s2, n2; tunerFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) tunerBuf[(size_t) s1] = tl;
            tunerFifo.finishedWrite (n1);
        }
        float v = inHp.process (di);
        // gate: il rilevatore legge la DI pulita PRIMA del notch (un notch a Q 40 "suona" per centinaia di ms
        // dopo lo stop e terrebbe aperto il gate)
        gateG[(size_t) juce::jmin (i, 4095)] = gateOn ? det.process (v, i) : 1.0f;   // gate spento: tutto aperto
        if (hum) v -= notch50.process (v) + notch100.process (v);   // notch = sottrazione del passa-banda stretto
        det.trackClean (v);
        snrG[(size_t) juce::jmin (i, 4095)] = gateOn ? det.snrDb() : 100.0f;
        // anti-fruscio d'ingresso: un pickup non produce quasi nulla sopra i 6 kHz, mentre il fruscio bianco della DI sta
        // per l'80 % proprio lì e il preamp lo alzerebbe di 60 dB (il "fizz" sotto le code): passa-basso a 2 poli a 6,5 kHz
        v = diLp.process (v);
        x[i] = v * gateG[(size_t) juce::jmin (i, 4095)];             // gate A: VCA pre-gain
    }
    // boost (pedale davanti all'ampli, dopo il gate A così il fruscio non sale nelle pause)
    if (boostGainSm.isSmoothing() || boostGainSm.getTargetValue() > 1.0f || boostEqSm.isSmoothing() || boostEqSm.getTargetValue() > 0.0f)
        for (int i = 0; i < n; ++i)
        {
            const float gB = boostGainSm.getNextValue(), m = boostEqSm.getNextValue();
            const float v = x[i] * gB;
            // livello alto: passa-alto, articolazione, niente fizz, drive leggerissimo (ADAA, ~1 dB di compressione sui picchi)
            float e = boostFizz.process (boostArt.process (v - boostLowCut.lp (v)));
            e = boostShaper.adaa (e * 1.25f, boostPrev) / 1.25f;
            x[i] = v + m * (e - v);
        }
    // bright cap sul potenziometro del gain (crunch: 470p; lead: 250p): con il gain basso passano soprattutto
    // le alte (suono brillante), alzandolo il condensatore conta sempre meno: a gain pieno l'effetto sparisce
    if (curChannel != Clean)
    {
        if (std::abs (gain01 - lastBright) > 0.002f) { brightCap.set (sp::Bq::HighShelf, fs, 1200.0, 0.7071, 9.0 * (1.0 - gain01) * (1.0 - gain01)); lastBright = gain01; }
        for (int i = 0; i < n; ++i) x[i] = brightCap.process (x[i]);
    }
    // potenziometro del gain (0 = silenzio)
    potSm.setTargetValue (potFade (gain01));
    if (potSm.isSmoothing() || potSm.getTargetValue() < 1.0f)
        for (int i = 0; i < n; ++i) x[i] *= potSm.getNextValue();
    // pre-gain del canale
    if (useBoost) for (int i = 0; i < n; ++i) x[i] = boostMid.process (x[i] - boostHp.lp (x[i]));
    else if (useBright || curChannel == Crunch) for (int i = 0; i < n; ++i) x[i] = brightShelf.process (x[i]);

    // stadi ADAA a oversampling (o, con l'oversampling spento, alla frequenza della sessione: l'ADAA tiene
    // comunque basso l'aliasing, che resta un po' più udibile solo sul lead con molto gain)
    {
        juce::dsp::AudioBlock<float> blk (mono.getArrayOfWritePointers(), 1, (size_t) n);
        auto& osp = osFactor == 4 ? os4 : os2;
        auto up = osFactor == 1 ? blk : osp.processSamplesUp (blk);
        float* u = up.getChannelPointer (0);
        const int un = (int) up.getNumSamples(), ns = kStages[curChannel];
        float clipAcc = 0;
        for (int i = 0; i < un; ++i)
        {
            float v = u[i];
            for (int k = 0; k < ns; ++k) v = stages[(size_t) k].process (v);
            // cathode follower (crunch, lead): pilota il tone stack e comprime morbido le semionde positive
            if (curChannel != Clean) v = cfDc.process (v > 0.0f ? v / (1.0f + 0.3f * v) : v);
            u[i] = v;
        }
        for (int k = 0; k < ns; ++k) clipAcc = juce::jmax (clipAcc, stages[(size_t) k].clip);
        satEnv.store (clipAcc);
        if (osFactor != 1) osp.processSamplesDown (blk);
    }

    // gate B (VCA + passa-basso dinamico 12 kHz -> 3 kHz quando chiude), tone stack, master, finale, cassa, level
    if (std::abs (bass - lastB) > 0.002f || std::abs (mid - lastM) > 0.002f || std::abs (treb - lastT) > 0.002f)
    {
        tone.set (fs, treb, mid, bass); lastB = bass; lastM = mid; lastT = treb;
        // clean: controlli ampi su un passivo a medie piene (a ore 12 quasi piatto, alti un po' vetrosi)
        cleanEq[0].set (sp::Bq::LowShelf, fs, 120.0, 0.7071, 24.0 * (bass - 0.5));
        cleanEq[1].set (sp::Bq::Peak, fs, 650.0, 0.7, 16.0 * (mid - 0.5) - 2.0);
        cleanEq[2].set (sp::Bq::HighShelf, fs, 2200.0, 0.7071, 20.0 * (treb - 0.5) + 2.0);
    }
    const bool mute = tunerMute.load (std::memory_order_relaxed);
    const float muteStep = 1.0f / (0.01f * (float) fs);
    double outPow = 0;
    const float sagHead = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = gateG[(size_t) juce::jmin (i, 4095)];
        if ((i & 15) == 0)
        {
            // gate B: il passa-basso dopo l'ultimo stadio segue il gate e il rapporto segnale/rumore della DI:
            // finché la nota è ben sopra il rumore resta a 12 kHz, mentre la coda scende verso il fruscio si chiude
            // fino a 3,5 kHz (il fruscio amplificato dai 60 dB del preamp sparisce sotto le code)
            const float snr = snrG[(size_t) juce::jmin (i, 4095)];
            const float clean = juce::jlimit (0.0f, 1.0f, (snr - 12.0f) / 30.0f);
            hissCut = 3500.0f + 8500.0f * g * clean;
            hissLp.set (fs, hissCut); hissLp2.set (fs, hissCut);
        }
        float v = hissLp2.lp (hissLp.lp (x[i])) * g;   // gate B: VCA + passa-basso dinamico a 12 dB/ott dopo l'ultimo stadio
        if (curChannel == Clean) v = cleanEq[2].process (cleanEq[1].process (cleanEq[0].process (v)));
        else v = tone.process (v) * 3.0f;                       // il tone stack passivo perde ~10 dB al centro
        v = voiceEq.process (tubeEq.process (v));
        v *= masterSm.getNextValue() * kChannelTrim[curChannel];   // volume del canale (prima del finale, come negli ampli veri)
        // finale: sag (RMS 10/150 ms) riduce l'headroom fino al 15 % e compensa il guadagno medio
        const float p2 = v * v;
        sagEnv = p2 > sagEnv ? sagA * (sagEnv - p2) + p2 : sagR * (sagEnv - p2) + p2;
        const float sagAmt = curChannel == Clean ? 0.02f : 0.15f;
        const float head = sagHead * tubeHead * (1.0f - sagAmt * juce::jmin (1.0f, std::sqrt (sagEnv) * 2.0f));
        // push-pull leggermente sbilanciato (2ª armonica quando satura) + trasformatore d'uscita: le basse lo saturano
        // per prime a volume alto (il "bloom" e la compressione del fondo di un finale vero)
        v = paDc.process (pclip.process (v + paAsym * head, head) - pclip.offset (paAsym * head, head)) / head;
        {
            const float lo = otLow.lp (v);
            v += (sp::fastTanh (lo * 1.3f) / 1.3f - lo);
        }
        // cassa: risonanza 96 Hz (che si comprime sui picchi), presence, LP di sicurezza
        v = safeLp.process (presShelf.process (resPeak.process (v)));
        // cambio canale: rampa
        if (pendingChannel >= 0)
        {
            chanRamp -= rampStep;
            if (chanRamp <= 0.0f)
            {
                chanRamp = 0.0f; osFactor = osWant; rampHold = true;   // il resto del blocco è del canale vecchio: resta a zero
                configureChannel (pendingChannel, tight); resetChannelState(); pendingChannel = -1;
                setLatencySamples ((int) std::ceil (osLatency()));
            }
        }
        else if (chanRamp < 1.0f && ! rampHold) chanRamp = juce::jmin (1.0f, chanRamp + rampStep);
        v *= chanRamp;
        // level
        v *= levelSm.getNextValue();
        muteG += ((mute ? 0.0f : 1.0f) - muteG) * muteStep;
        v *= muteG;
        outPow += (double) v * v;
        for (int c = 0; c < numOut; ++c) buffer.getWritePointer (c)[i] = v;
    }
    // gain a 0 (potenziometro chiuso e fermo): silenzio esatto, nemmeno il residuo numerico della polarizzazione
    // degli stadi amplificato da quelli dopo
    if (! potSm.isSmoothing() && potSm.getTargetValue() <= 0.0f) { buffer.clear(); outPow = 0; }
    gateGain.store (gateG[(size_t) juce::jmin (n - 1, 4095)]);
    outRmsDb.store (outPow > 0 ? (float) (10.0 * std::log10 (outPow / n)) : -100.0f);
    for (auto* f : { &voiceEq, &boostArt, &boostFizz, &brightCap, &tubeEq, &cleanEq[0], &cleanEq[1], &cleanEq[2], &diLp, &inHp, &notch50, &notch100, &tunerLp, &resPeak, &presShelf, &safeLp, &boostMid, &brightShelf }) f->flush();
    tone.flush();
}

// stato: i parametri e, in HEADS, i controlli delle altre due testate
void BAmpProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto t = apvts.copyState();
    if (auto old = t.getChildWithName ("HEADS"); old.isValid()) t.removeChild (old, nullptr);
    t.appendChild (headMemory.save ("HEADS"), nullptr);
    t.setProperty ("osOrder", 2, nullptr);   // oversampling nell'ordine off, 2x, 4x
    if (auto x = t.createXml()) copyXmlToBinary (*x, d);
}

void BAmpProcessor::setStateInformation (const void* d, int s)
{
    auto x = getXmlFromBinary (d, s);
    if (x == nullptr || ! x->hasTagName (apvts.state.getType())) return;
    auto t = juce::ValueTree::fromXml (*x);
    auto heads = t.getChildWithName ("HEADS");
    if (heads.isValid()) t.removeChild (heads, nullptr);
    // prima l'oversampling era 2x (0) e 4x (1): ora off (0), 2x (1), 4x (2)
    if ((int) t.getProperty ("osOrder", 1) < 2)
    {
        auto remap = [] (float v) { return v < 0.5f ? 1.0f : 2.0f; };
        if (auto os = t.getChildWithProperty ("id", BAmpIDs::os); os.isValid()) os.setProperty ("value", remap ((float) os["value"]), nullptr);
        for (auto c : heads) if (c.hasProperty (BAmpIDs::os)) c.setProperty (BAmpIDs::os, remap ((float) c[BAmpIDs::os]), nullptr);
        t.setProperty ("osOrder", 2, nullptr);
    }
    // progetti di prima: il noise gate era acceso di default e non veniva scritto se non toccato
    const bool hadGate = [&] { for (auto c : t) if (c["id"].toString() == BAmpIDs::gateOn) return true; return false; }();
    apvts.replaceState (t);
    if (! hadGate) if (auto* g = apvts.getParameter (BAmpIDs::gateOn)) g->setValueNotifyingHost (1.0f);
    headMemory.load (heads);
}
juce::AudioProcessorEditor* BAmpProcessor::createEditor() { return new BAmpEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BAmpProcessor(); }
