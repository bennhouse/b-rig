// b-rig: lo studio standalone della b-suite. Un solo eseguibile con dentro i motori DSP e le interfacce
// dei plugin (nessuna scansione di VST3/AU esterni).
#include "UI/RigComponent.h"

#if JUCE_MAC
void rigStyleTitleBar (juce::Component&);
#endif

namespace
{
class RigAudio final : public juce::AudioIODeviceCallback
{
public:
    explicit RigAudio (rig::RigEngine& e) : engine (e) {}
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        engine.process (in, numIn, out, numOut, n);
    }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { engine.prepare (d->getCurrentSampleRate(), d->getCurrentBufferSizeSamples()); }
    void audioDeviceStopped() override { engine.release(); }
private:
    rig::RigEngine& engine;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (rig::RigEngine& e, juce::AudioDeviceManager& dm)
        : DocumentWindow ("b-rig", ui::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new rig::RigComponent (e, &dm), true);
        setResizable (true, false);
        setResizeLimits (1180, 700, 8000, 6000);
        centreWithSize (1180, 760);
        setVisible (true);
       #if JUCE_MAC
        rigStyleTitleBar (*this);
       #endif
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};
}

class BRigApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "b-rig"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "b-rig";
        o.folderName = "b-suite";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);

        // il preset scelto per l'avvio (all'inizio "clean slate", la catena vuota), prima di aprire l'audio
        rig::RigComponent::loadStartup (engine);
        engine.diChannel = props->getIntValue ("di", 0);
        // metronomo: acceso/spento, suono e volume restano uguali da una sessione all'altra
        engine.metroOn = props->getBoolValue ("metroOn", false);
        engine.metroSound = props->getIntValue ("metroSound", 0);
        engine.metroVolume = (float) props->getDoubleValue ("metroVolume", 0.5);
        engine.bpm = (float) props->getDoubleValue ("bpm", 120.0);
        engine.sigIndex = props->getIntValue ("sig", 2);
        // tuner: muta l'uscita quando si accende, a meno che l'utente non abbia scelto di no (resta tra le sessioni)
        engine.tunerMutes = props->getBoolValue ("tunerMutes", true);
        // tutti gli ingressi aperti (fino a 16): la DI si sceglie in audio setup senza riaprire il dispositivo
        const auto saved = props->getXmlValue ("audio");
        deviceManager.initialise (16, 2, saved.get(), true);
        if (saved == nullptr)   // primo avvio: buffer di 64 campioni (o il più vicino che la scheda accetta)
            if (auto* dev = deviceManager.getCurrentAudioDevice())
            {
                auto st = deviceManager.getAudioDeviceSetup();
                int best = st.bufferSize;
                for (int b : dev->getAvailableBufferSizes()) if (std::abs (b - 64) < std::abs (best - 64)) best = b;
                if (best != st.bufferSize) { st.bufferSize = best; deviceManager.setAudioDeviceSetup (st, true); }
            }
        deviceManager.addAudioCallback (&audio);
        window = std::make_unique<MainWindow> (engine, deviceManager);
        // primo avvio: una finestra spiega che b-rig non contiene IR di casse (si vede una volta sola)
        if (! props->getBoolValue ("welcomeShown", false))
            if (auto* rc = dynamic_cast<rig::RigComponent*> (window->getContentComponent()))
            {
                rc->showWelcome();
                props->setValue ("welcomeShown", true);
                props->saveIfNeeded();
            }
    }

    void shutdown() override
    {
        if (auto xml = deviceManager.createStateXml()) props->setValue ("audio", xml.get());
        props->setValue ("di", engine.diChannel.load());
        props->setValue ("metroOn", engine.metroOn.load());
        props->setValue ("metroSound", engine.metroSound.load());
        props->setValue ("metroVolume", (double) engine.metroVolume.load());
        props->setValue ("bpm", (double) engine.bpm.load());
        props->setValue ("sig", engine.sigIndex.load());
        props->setValue ("tunerMutes", engine.tunerMutes.load());
        props->saveIfNeeded();
        deviceManager.removeAudioCallback (&audio);
        deviceManager.closeAudioDevice();
        window.reset();     // prima le interfacce dei moduli, poi i moduli
        engine.clear();
        engine.collectGarbage();
    }

    void systemRequestedQuit() override { quit(); }

private:
    rig::RigEngine engine;
    RigAudio audio { engine };
    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::PropertiesFile> props;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (BRigApplication)
