#include "RigPlugin.h"

namespace rig
{
RigPluginProcessor::RigPluginProcessor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::mono(), true)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    engine.metroOn = false;   // il metronomo è della DAW
    engine.diChannel = 0;
    // un'istanza nuova parte dal preset scelto per l'avvio nell'app ("load at start-up"); altrimenti dall'ultimo stato
    // del plugin, com'era (anche modificato e non salvato); la prima volta clean slate. Un progetto della DAW poi
    // rimette il suo stato con setStateInformation.
    const auto n = RigComponent::startupPreset();
    bool loaded = false;
    if (RigComponent::hasStartupChoice())
    {
        if (n != RigComponent::kCleanSlate && RigComponent::loadPresetFile (engine, RigComponent::presetFile (n))) { presetName = n; loaded = true; }
    }
    else if (auto x = juce::XmlDocument::parse (RigComponent::pluginSessionFile()))
    {
        const auto t = juce::ValueTree::fromXml (*x);
        if (t.hasType ("BRIG")) { loadState (t); loaded = true; }
    }
    if (! loaded) engine.clear();
    lastWritten = saveState().toXmlString();
    startTimerHz (4);
}

RigPluginProcessor::~RigPluginProcessor()
{
    stopTimer();
    writeSessionIfChanged();
    engine.clear();
    engine.collectGarbage();
}

// sempre mono -> stereo; un ingresso stereo (le tracce di alcune DAW lo sono sempre) si somma in mono
bool RigPluginProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void RigPluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());
}

void RigPluginProcessor::releaseResources() { engine.release(); }

void RigPluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0 || buffer.getNumChannels() < 2) { buffer.clear(); return; }
    auto* const* w = buffer.getArrayOfWritePointers();
    if (getTotalNumInputChannels() >= 2)
    {
        juce::FloatVectorOperations::add (w[0], w[1], n);
        juce::FloatVectorOperations::multiply (w[0], 0.5f, n);
    }
    engine.hostPlayHead = getPlayHead();   // i moduli in sync seguono il tempo del progetto
    engine.process (buffer.getArrayOfReadPointers(), 1, w, buffer.getNumChannels(), n);
}

void RigPluginProcessor::timerCallback()
{
    {
        juce::ValueTree t;
        { const juce::ScopedLock sl (pendingLock); std::swap (t, pending); }
        if (t.isValid()) loadState (t);
    }
    engine.collectGarbage();
    if (++sessionTick % 8 == 0) writeSessionIfChanged();   // ogni 2 s, solo se qualcosa è cambiato
    // la latenza dei moduli accesi (oversampling di b-amp, b-drop...): la DAW la compensa
    if (const int lat = engine.latencySamples(); lat != getLatencySamples()) setLatencySamples (lat);
}

void RigPluginProcessor::writeSessionIfChanged()
{
    const auto xml = saveState().toXmlString();
    if (xml == lastWritten) return;
    lastWritten = xml;
    const auto f = RigComponent::pluginSessionFile();
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);   // prima un file temporaneo, poi lo scambio: mai un file a metà
    if (tmp.getFile().replaceWithText (xml)) tmp.overwriteTargetFileWithTemporary();
}

juce::ValueTree RigPluginProcessor::saveState() const
{
    auto t = engine.saveRig();
    int sel = selectedIndex;
    if (auto* ed = dynamic_cast<RigPluginEditor*> (getActiveEditor()); ed != nullptr && ed->hasRig()) sel = engine.indexOf (ed->rig().getSelected());
    t.setProperty ("selected", sel, nullptr);
    t.setProperty ("preset", presetName, nullptr);
    t.setProperty ("dirty", presetDirty, nullptr);
    t.setProperty ("editorW", editorW, nullptr);
    t.setProperty ("editorH", editorH, nullptr);
    return t;
}

void RigPluginProcessor::loadState (const juce::ValueTree& t)
{
    if (! t.hasType ("BRIG")) return;
    if (onStateLoading) onStateLoading();   // l'interfaccia chiude il modulo aperto prima che la catena cambi
    engine.loadRig (t);
    presetName = t.getProperty ("preset", RigComponent::kCleanSlate).toString();
    presetDirty = (bool) t.getProperty ("dirty", false);
    selectedIndex = (int) t.getProperty ("selected", -1);
    editorW = juce::jmax (1180, (int) t.getProperty ("editorW", 1180));
    editorH = juce::jmax (700, (int) t.getProperty ("editorH", 760));
    setLatencySamples (engine.latencySamples());
    lastWritten = saveState().toXmlString();   // lo stato di un progetto riaperto non è una modifica
    if (onStateLoaded) onStateLoaded();
}

void RigPluginProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto x = saveState().createXml()) copyXmlToBinary (*x, dest);
}

void RigPluginProcessor::setStateInformation (const void* data, int size)
{
    const auto x = getXmlFromBinary (data, size);
    if (x == nullptr) return;
    const auto t = juce::ValueTree::fromXml (*x);
    if (juce::MessageManager::existsAndIsCurrentThread()) loadState (t);
    else { const juce::ScopedLock sl (pendingLock); pending = t; }   // la catena si cambia solo dal thread dei messaggi
}

juce::AudioProcessorEditor* RigPluginProcessor::createEditor() { return new RigPluginEditor (*this); }

// ---------------------------------------------------------------- interfaccia

RigPluginEditor::RigPluginEditor (RigPluginProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
{
    rc = std::make_unique<RigComponent> (p.engine, nullptr, true);
    addAndMakeVisible (*rc);
    auto restore = [this]
    {
        rc->setPresetState (proc.presetName, proc.presetDirty);
        if (juce::isPositiveAndBelow (proc.selectedIndex, proc.engine.numSlots())) rc->select (&proc.engine.slot (proc.selectedIndex));
    };
    restore();
    rc->onPresetState = [this] (const juce::String& name, bool dirty) { proc.presetName = name; proc.presetDirty = dirty; };
    proc.onStateLoading = [this] { rc->select (nullptr); };
    proc.onStateLoaded = restore;
    setResizable (true, true);
    setResizeLimits (1180, 700, 8000, 6000);
    setSize (proc.editorW, proc.editorH);
}

RigPluginEditor::~RigPluginEditor()
{
    proc.onStateLoaded = nullptr;
    proc.onStateLoading = nullptr;
    proc.selectedIndex = proc.engine.indexOf (rc->getSelected());
    proc.presetName = rc->currentPreset;
    proc.presetDirty = rc->dirty;
    proc.writeSessionIfChanged();   // prima di chiudere l'interfaccia: saveState() legge ancora il modulo selezionato
    rc.reset();
}

void RigPluginEditor::resized()
{
    rc->setBounds (getLocalBounds());
    proc.editorW = getWidth(); proc.editorH = getHeight();
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new rig::RigPluginProcessor(); }
