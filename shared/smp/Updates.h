#pragma once
// Controllo degli aggiornamenti, uguale per b-rig e per tutti i plugin.
//
// Le versioni pubblicate sono le release del repository GitHub (kUpdateRepo), con un tag per prodotto:
// "b-rig-v0.2.0" per l'app, "b-suite-v0.2.0" per tutti i plugin (un solo installer, una sola versione). Una sola richiesta all'API delle release restituisce tutti i prodotti:
// il risultato si salva in "b-suite/updates.settings" (condiviso tra app e plugin, anche in processi diversi)
// e la richiesta si ripete al massimo una volta ogni 24 ore. Durante la beta contano anche le pre-release.
//
// Nessun dato dell'utente esce dal computer: è una GET anonima con un User-Agent generico.
// Se il repository non è raggiungibile (privato, offline) non succede nulla: nessun avviso, nessun errore.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>

namespace ui::updates
{
// "proprietario/repository" delle release; vuoto = controllo spento
inline constexpr const char* kUpdateRepo = "bennhouse/b-suite";
inline constexpr bool kBeta = true;                       // tutta la suite è in beta
inline constexpr juce::int64 kCheckEveryMs = 24 * 60 * 60 * 1000;

// versione di questo binario (plugin: da CMake; app: JUCE_APPLICATION_VERSION_STRING)
inline juce::String currentVersion()
{
   #if defined (JucePlugin_VersionString)
    return JucePlugin_VersionString;
   #elif defined (JUCE_APPLICATION_VERSION_STRING)
    return JUCE_APPLICATION_VERSION_STRING;
   #else
    return {};
   #endif
}

inline juce::String versionLabel (const juce::String& v) { return v.isEmpty() ? juce::String() : v + (kBeta ? " beta" : ""); }

// confronto di versioni "1.2.3" (numeri, da sinistra): > 0 se a è più nuova di b
inline int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto pa = juce::StringArray::fromTokens (a, ".", ""), pb = juce::StringArray::fromTokens (b, ".", "");
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();
        if (x != y) return x > y ? 1 : -1;
    }
    return 0;
}

// Dall'elenco JSON delle release: per ogni prodotto la versione più alta e la pagina da cui scaricarla.
// Esclude le bozze; durante la beta include le pre-release.
inline juce::var latestPerProduct (const juce::var& releases)
{
    auto* out = new juce::DynamicObject();
    if (auto* arr = releases.getArray())
        for (const auto& r : *arr)
        {
            if ((bool) r["draft"] || ((bool) r["prerelease"] && ! kBeta)) continue;
            const auto tag = r["tag_name"].toString();
            const int v = tag.lastIndexOf ("-v");
            if (! tag.startsWith ("b-") || v <= 2) continue;
            const auto product = tag.substring (0, v), version = tag.substring (v + 2);
            if (! version.containsOnly ("0123456789.") || version.isEmpty()) continue;
            const auto prev = out->getProperty (product);
            if (prev.isObject() && compareVersions (version, prev["version"].toString()) <= 0) continue;
            auto* e = new juce::DynamicObject();
            e->setProperty ("version", version);
            e->setProperty ("url", r["html_url"].toString());
            out->setProperty (product, juce::var (e));
        }
    return juce::var (out);
}

struct Available { juce::String version, url; };

// Su un thread a parte (mai sul thread dei messaggi né su quello audio): legge la cache o, se è vecchia,
// interroga GitHub. shouldExit interrompe il download (chiusura dell'editor).
inline Available check (const juce::String& product, const juce::String& current, std::function<bool()> shouldExit)
{
    if (juce::String (kUpdateRepo).isEmpty() || current.isEmpty()) return {};
    juce::PropertiesFile::Options o;
    o.applicationName = "updates";
    o.folderName = "b-suite";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    juce::InterProcessLock lock ("b-suite-updates");
    o.processLock = &lock;
    juce::PropertiesFile cache (o);

    const auto now = juce::Time::currentTimeMillis();
    auto latest = juce::JSON::parse (cache.getValue ("latest"));
    const auto checkedAt = cache.getValue ("checkedAt").getLargeIntValue();
    if (! latest.isObject() || now - checkedAt > kCheckEveryMs || now < checkedAt)
    {
        const juce::URL url ("https://api.github.com/repos/" + juce::String (kUpdateRepo) + "/releases?per_page=100");
        int status = 0;
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs (5000)
                        .withStatusCode (&status)
                        .withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: b-suite-update-check")
                        .withProgressCallback ([shouldExit] (int, int) { return ! shouldExit(); });
        if (auto stream = url.createInputStream (opts); stream != nullptr && status == 200 && ! shouldExit())
        {
            latest = latestPerProduct (juce::JSON::parse (stream->readEntireStreamAsString()));
            cache.setValue ("latest", juce::JSON::toString (latest, true));
        }
        // anche se fallisce (repo privato, offline) si riprova solo domani: niente richieste a ogni apertura
        cache.setValue ("checkedAt", juce::String (now));
        cache.saveIfNeeded();
    }
    const auto e = latest[juce::Identifier (product)];
    if (e.isObject() && compareVersions (e["version"].toString(), current) > 0)
        return { e["version"].toString(), e["url"].toString() };
    return {};
}

// Avvia il controllo quando la finestra è davvero sullo schermo (non nei test, non negli snapshot) e
// richiama onFound sul thread dei messaggi se c'è una versione più nuova. Si ferma da solo alla distruzione.
class Watcher final : private juce::Timer, private juce::Thread
{
public:
    Watcher (juce::Component& c, juce::String productName, std::function<void (const Available&)> found)
        : juce::Thread ("b-suite update check"), comp (c), product (std::move (productName)), onFound (std::move (found))
    {
        if (juce::String (kUpdateRepo).isNotEmpty()) startTimer (1000);
    }
    ~Watcher() override { stopTimer(); signalThreadShouldExit(); stopThread (6000); }

private:
    void timerCallback() override
    {
        if (comp.getPeer() == nullptr || ! comp.isShowing()) return;
        stopTimer();
        startThread (juce::Thread::Priority::low);
    }
    void run() override
    {
        const auto a = check (product, currentVersion(), [this] { return threadShouldExit(); });
        if (a.version.isEmpty() || threadShouldExit()) return;
        juce::Component::SafePointer<juce::Component> safe (&comp);
        auto cb = onFound;
        juce::MessageManager::callAsync ([safe, cb, a] { if (safe != nullptr && cb) cb (a); });
    }
    juce::Component& comp;
    juce::String product;
    std::function<void (const Available&)> onFound;
};
}
