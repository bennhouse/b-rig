#include "PluginProcessor.h"
#include "PluginEditor.h"

BSceneryProcessor::BSceneryProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BScenery", createLayout())
{
    for (int l = 0; l < 4; ++l) { pOn[l] = apvts.getRawParameterValue (BSceneryIDs::on (l)); pLvl[l] = apvts.getRawParameterValue (BSceneryIDs::level (l)); }
    pMix = apvts.getRawParameterValue (BSceneryIDs::mix);
    pReact = apvts.getRawParameterValue (BSceneryIDs::react);
    pMains = apvts.getRawParameterValue (BSceneryIDs::mains);
    startTimerHz (20);
}

juce::AudioProcessorValueTreeState::ParameterLayout BSceneryProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    const char* names[4] = { "Vinyl", "Amp hum", "Rain", "Lo-fi" };
    for (int i = 0; i < 4; ++i)
    {
        l.add (std::make_unique<AudioParameterBool> (ParameterID { BSceneryIDs::on (i), 1 }, String (names[i]) + " on", i == 0));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSceneryIDs::level (i), 1 }, String (names[i]) + " level",
                                                      NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, AudioParameterFloatAttributes().withLabel ("%")));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSceneryIDs::mix, 1 }, "Texture gain", NormalisableRange<float> (-60.0f, 6.0f, 0.1f), -20.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { BSceneryIDs::react, 1 }, "React", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { BSceneryIDs::mains, 1 }, "Mains", StringArray { "50 Hz", "60 Hz" }, 0));
    return l;
}

bool BSceneryProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void BSceneryProcessor::prepareToPlay (double rate, int)
{
    fs = rate;
    auto setF = [this] (sp::Bq& f, sp::Bq::Kind k, double hz, double q) { f = {}; f.set (k, fs, hz, q); };
    setF (clickHp, sp::Bq::HP, 1200.0, 0.7071); setF (clickBp, sp::Bq::BP, 3500.0, 0.8); setF (popLp, sp::Bq::LP, 600.0, 0.9);
    setF (hissHpL, sp::Bq::HP, 1000.0, 0.7071); hissHpR = hissHpL; setF (hissLpL, sp::Bq::LP, 9000.0, 0.7071); hissLpR = hissLpL;
    setF (rumbleLp, sp::Bq::LP, 30.0, 0.7071);
    setF (humBuzzLp, sp::Bq::LP, 1500.0, 0.7071); setF (humHissHp, sp::Bq::HP, 2000.0, 0.7071); setF (humHissLp, sp::Bq::LP, 9000.0, 0.7071);
    setF (washHpL, sp::Bq::HP, 200.0, 0.7071); washHpR = washHpL; setF (washLpL, sp::Bq::LP, 7000.0, 0.7071); washLpR = washLpL;
    const double bankHz[4] = { 1800.0, 3200.0, 5500.0, 900.0 }, bankMs[4] = { 1.5, 1.2, 0.9, 6.0 };
    for (int k = 0; k < 4; ++k) { setF (dropBank[(size_t) k], sp::Bq::BP, bankHz[k], k == 3 ? 3.0 : 1.5); dropDecay[(size_t) k] = sp::coefMs (bankMs[k], fs); dropEnv[(size_t) k] = 0; }
    auto rot = [this] (double hz, float& c, float& sn) { c = (float) std::cos (juce::MathConstants<double>::twoPi * hz / fs); sn = (float) std::sin (juce::MathConstants<double>::twoPi * hz / fs); };
    rot (33.333 / 60.0, rotCs, rotSn); rot (0.13, wobRC, wobRS); rot (1.2, wowRC, wowRS);
    rotS = wobS = wowS = hs = 0.0f; rotC = wobC = wowC = hc = 1.0f;
    for (int c = 0; c < 2; ++c) { lofiHp[(size_t) c] = {}; lofiHp[(size_t) c].set (sp::Bq::HP, fs, 180.0, 0.5); lofiLp[(size_t) c] = {}; lofiLp[(size_t) c].set (sp::Bq::LP, fs, 4200.0, 0.5); }
    for (auto& b : lofiBuf) b.fill (0.0f);
    eA = sp::coefMs (4.0, fs); eR = sp::coefMs (220.0, fs);
    fadeC = 1.0f - sp::coefMs (35.0 / 4.6, fs);   // ~35 ms fino al silenzio (-40 dB)
    gainC = 1.0f - sp::coefMs (100.0 / 4.6, fs);  // rampa di ~100 ms sui cambi di livello
    texGain = juce::Decibels::decibelsToGain (pMix->load());
    envG = active = 0; listenLeft.store (0);
}

bool BSceneryProcessor::applyBalanceIfReady()
{
    if (! balanceReady.exchange (false)) return false;
    auto* p = apvts.getParameter (BSceneryIDs::mix);
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-60.0f, 6.0f, balanceResult.load())));
    p->endChangeGesture();
    return true;
}

void BSceneryProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int numIn = getTotalNumInputChannels(), n = buffer.getNumSamples();
    bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) playing = pos->getIsPlaying();
    if (const int cmd = balanceCmd.exchange (0); cmd == 1) { listenLeft.store ((int) (2.5 * fs)); accG = accT = 0; accN = 0; }
    else if (cmd == 2) listenLeft.store (0);

    float lv[4]; bool on[4];
    for (int l = 0; l < 4; ++l) { on[l] = pOn[l]->load() > 0.5f; lv[l] = on[l] ? pLvl[l]->load() / 100.0f : 0.0f; }
    const float react = pReact->load() / 100.0f, gainT = juce::Decibels::decibelsToGain (pMix->load());
    const double humHz = pMains->load() > 0.5f ? 60.0 : 50.0;
    hRotC = (float) std::cos (juce::MathConstants<double>::twoPi * humHz / fs); hRotS = (float) std::sin (juce::MathConstants<double>::twoPi * humHz / fs);
    layerPk.fill (0.0f);
    const float thrRef = 0.05f, gate65 = juce::Decibels::decibelsToGain (-65.0f), gate50 = juce::Decibels::decibelsToGain (-50.0f);
    const float wowDepth = lv[3] * 0.0018f * (float) fs, srTarget = 1.0f - lv[3] * (1.0f - 14000.0f / 48000.0f);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    int listen = listenLeft.load();

    for (int i = 0; i < n; ++i)
    {
        const float gl = L[i], gr = numIn > 1 ? R[i] : L[i];
        const float g2 = 0.5f * (gl * gl + gr * gr);
        envG = g2 > envG ? eA * (envG - g2) + g2 : eR * (envG - g2) + g2;
        const float gEnv = std::sqrt (envG);
        // attive solo in play o con la chitarra viva; altrimenti fade a zero in ~35 ms
        active += (((playing || gEnv > gate65) ? 1.0f : 0.0f) - active) * fadeC;
        if (active < 1e-5f) active = 0.0f;

        float tl = 0, tr = 0;
        // oscillatori lenti in quadratura (rotazione del disco, ondeggiare del ronzio)
        { const float a2 = rotS * rotCs + rotC * rotSn; rotC = rotC * rotCs - rotS * rotSn; rotS = a2; }
        { const float a2 = wobS * wobRC + wobC * wobRS; wobC = wobC * wobRC - wobS * wobRS; wobS = a2; }
        if (active > 0.0f)
        {
            // vinyl: crackle a legge di potenza, pop rari, soffio di superficie con la rotazione, rumble
            if (lv[0] > 0)
            {
                float imp = 0.0f, pan = 0.0f;
                if (u01() < 40.0f / (float) fs)
                {
                    const float u = u01();
                    imp = (0.03f + 0.97f * u * u * u * u * u * u) * (rnd() > 0.0f ? 1.0f : -1.0f);   // tanti micro-click, pochi forti
                    pan = 0.7f * rnd();
                }
                if (u01() < 0.4f / (float) fs) popImp = (0.5f + 0.5f * u01()) * (rnd() > 0.0f ? 3.0f : -3.0f);
                const float click = clickBp.process (clickHp.process (imp * 6.0f));
                crackL = click * (1.0f - pan); crackR = click * (1.0f + pan);
                const float pop = popLp.process (popImp); popImp = 0.0f;
                const float rotAmt = 0.82f + 0.18f * rotS;   // il disco gira: il soffio respira a 0,56 Hz
                const float hL = hissLpL.process (hissHpL.process (pinkVL.next (rnd()))) * rotAmt;
                const float hR = hissLpR.process (hissHpR.process (pinkVR.next (rnd()))) * rotAmt;
                const float rum = rumbleLp.process (rnd()) * 1.5f;
                const float vl = crackL + 0.8f * pop + 0.5f * hL + rum, vr = crackR + 0.8f * pop + 0.5f * hR + rum;
                tl += lv[0] * vl; tr += lv[0] * vr;
                layerPk[0] = juce::jmax (layerPk[0], std::abs (vl));
            }
            // hum: fondamentale debole + ripple del raddrizzatore (buzz a 100/120 Hz) + dispari, ampiezza che ondeggia
            if (lv[1] > 0)
            {
                const float s2 = hs * hRotC + hc * hRotS; hc = hc * hRotC - hs * hRotS; hs = s2;
                const float rect = std::abs (hs) - 0.63662f;
                const float buzz = humBuzzLp.process (rect);
                const float h = (0.18f * hs + 0.6f * buzz + 0.12f * hs * hs * hs) * (0.85f + 0.15f * wobS);
                const float v = 0.3f * h + 0.04f * humHissLp.process (humHissHp.process (rnd()));
                tl += lv[1] * v; tr += lv[1] * v;
                layerPk[1] = juce::jmax (layerPk[1], std::abs (v));
            }
            // rain: tappeto rosa con raffiche + tante gocce su risonanze fisse (niente coefficienti per goccia)
            if (lv[2] > 0)
            {
                if ((i & 1023) == 0) gustT = 0.45f + 0.55f * u01();   // raffiche: nuovo livello ogni ~21 ms, inseguito lentamente
                gust += 0.00002f * (gustT - gust);
                const float wl = washLpL.process (washHpL.process (pinkL.next (rnd()))) * gust;
                const float wr = washLpR.process (washHpR.process (pinkR.next (rnd()))) * gust;
                if (u01() < 260.0f / (float) fs)
                {
                    const int k = juce::jlimit (0, 2, (int) (u01() * 3.0f));
                    const float u = u01();
                    dropEnv[(size_t) k] = juce::jmax (dropEnv[(size_t) k], 0.05f + 0.6f * u * u * u);
                    dropPan[(size_t) k] = 0.8f * rnd();
                }
                if (u01() < 3.0f / (float) fs) { dropEnv[3] = 0.6f + 0.4f * u01(); dropPan[3] = 0.6f * rnd(); }
                float dl = 0.0f, dr = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    auto& e = dropEnv[(size_t) k];
                    const float y = dropBank[(size_t) k].process (e > 1.0e-5f ? e * rnd() : 0.0f);
                    e *= dropDecay[(size_t) k];
                    dl += y * (1.0f - dropPan[(size_t) k]); dr += y * (1.0f + dropPan[(size_t) k]);
                }
                const float vl = 0.45f * wl + 1.6f * dl, vr = 0.45f * wr + 1.6f * dr;
                tl += lv[2] * vl; tr += lv[2] * vr;
                layerPk[2] = juce::jmax (layerPk[2], std::abs (vl));
            }
        }
        // balance: misura chitarra (sopra -50 dBFS) e texture (prima di gain e ducking)
        if (listen > 0)
        {
            if (gEnv > gate50) { accG += g2; accT += 0.5 * (tl * tl + tr * tr); ++accN; }
            if (--listen == 0 && accN > 0)
            {
                const double target = std::sqrt (accG / accN) * 0.158489 / (std::sqrt (accT / accN) + 1e-7);
                balanceResult.store ((float) juce::Decibels::gainToDecibels (target));
                balanceReady.store (true);
            }
        }
        // react: le texture si abbassano quando plettri forte
        const float ratio = gEnv / thrRef;
        const float duck = 1.0f / (1.0f + react * 4.0f * ratio * ratio);
        texGain += gainC * (gainT - texGain);
        const float g = texGain * duck * active;
        float yl = gl + g * tl, yr = gr + g * tr;

        // lo-fi su tutto il bus: decimatore, wow & flutter, banda telefonica
        if (lv[3] > 0)
        {
            lofiBuf[0][(size_t) lofiW] = yl; lofiBuf[1][(size_t) lofiW] = yr;
            { const float a2 = wowS * wowRC + wowC * wowRS; wowC = wowC * wowRC - wowS * wowRS; wowS = a2; }
            jitter += 0.001f * (rnd() - jitter * 0.05f);
            const float d = 2.0f + wowDepth * (0.5f + 0.5f * wowS) + 0.2f * jitter * wowDepth;
            const int di = (int) d; const float t = d - (float) di;
            auto cub = [&] (const std::array<float, 4096>& b)   // interpolazione cubica (Hermite)
            {
                auto at = [&] (int o) { return b[(size_t) ((lofiW - di + o) & 4095)]; };
                const float xm1 = at (1), x0 = at (0), x1 = at (-1), x2 = at (-2);
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * t + c2) * t + c1) * t + x0;
            };
            float wl = cub (lofiBuf[0]), wr = cub (lofiBuf[1]);
            lofiW = (lofiW + 1) & 4095;
            srPh += srTarget; if (srPh >= 1.0f) { srPh -= 1.0f; holdL = wl; holdR = wr; }
            wl = lofiLp[0].process (lofiHp[0].process (holdL)); wr = lofiLp[1].process (lofiHp[1].process (holdR));
            yl += lv[3] * (wl - yl); yr += lv[3] * (wr - yr);
        }
        L[i] = yl; R[i] = yr;

        const float tAbs = std::sqrt (0.5f * (tl * tl + tr * tr));
        histG = juce::jmax (histG, gEnv); histT += g * g * tAbs * tAbs; histP += texGain * texGain * active * active * tAbs * tAbs;   // texture: RMS del fotogramma
        if (lv[3] > 0) layerPk[3] = juce::jmax (layerPk[3], lv[3] * std::abs (yl));
        if (++histCount >= kHop)
        {
            const int p = (historyPos.load (std::memory_order_relaxed) + 1) & (kHistory - 1);
            history[(size_t) p] = { histG, std::sqrt (histP / (float) kHop), std::sqrt (histT / (float) kHop) };
            historyPos.store (p, std::memory_order_release);
            histCount = 0; histG = histT = histP = 0;
        }
    }
    listenLeft.store (listen);
    duckLast = 0;
    for (int l = 0; l < 3; ++l) layerLevel[(size_t) l].store (layerPk[(size_t) l] * lv[l] * texGain * active);
    layerLevel[3].store (layerPk[3]);
    for (auto* f : { &clickHp, &clickBp, &popLp, &hissHpL, &hissHpR, &hissLpL, &hissLpR, &rumbleLp, &humBuzzLp, &humHissHp, &humHissLp,
                     &washHpL, &washHpR, &washLpL, &washLpR }) f->flush();
    for (auto& f : dropBank) f.flush();
    for (float* q : { &rotS, &wobS, &hs, &wowS })   // gli oscillatori in quadratura restano a raggio 1
    {
        float* c = q == &rotS ? &rotC : (q == &wobS ? &wobC : (q == &hs ? &hc : &wowC));
        const float m = 1.0f / std::sqrt (*q * *q + *c * *c); *q *= m; *c *= m;
    }
}

void BSceneryProcessor::getStateInformation (juce::MemoryBlock& d) { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
void BSceneryProcessor::setStateInformation (const void* d, int sz)
{
    if (auto x = getXmlFromBinary (d, sz)) if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
}
juce::AudioProcessorEditor* BSceneryProcessor::createEditor() { return new BSceneryEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BSceneryProcessor(); }
