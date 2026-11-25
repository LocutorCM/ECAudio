#include <JuceHeader.h>
#include "Engine.h"
#include "Theme.h"
#include "UI.h"

//==============================================================================
class MainComponent : public Component,
                      public MenuBarModel,
                      public ApplicationCommandTarget,
                      private ChangeListener,
                      private Timer
{
public:
    MainComponent()
        : arrange (engine),
          strip (engine, deviceManager, [this] { showPluginManager(); }),
          master (engine),
          transport (engine, deviceManager, commandManager)
    {
        // ---- audio: 128 muestras a 48 kHz por defecto (baja latencia)
        auto saved = parseXML (Engine::appDataDir().getChildFile ("audio.xml"));
        AudioDeviceManager::AudioDeviceSetup pref;
        pref.sampleRate = 48000.0;
        pref.bufferSize = 128;
        auto err = deviceManager.initialise (2, 2, saved.get(), true, {}, saved ? nullptr : &pref);
        if (err.isNotEmpty())
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Audio", err);

        player.setProcessor (&engine);
        deviceManager.addAudioCallback (&player);
        deviceManager.addChangeListener (this);
        engine.knownPlugins.addChangeListener (this);

        addAndMakeVisible (transport);
        addAndMakeVisible (arrange);
        addAndMakeVisible (strip);
        addAndMakeVisible (master);

        arrange.st.onSelectTrack = [this] (int i) { selectTrack (i); };
        arrange.onAddTrack = [this] { addTrack(); };

        commandManager.registerAllCommandsForTarget (this);
        commandManager.setFirstCommandTarget (this);
        addKeyListener (commandManager.getKeyMappings());
        setWantsKeyboardFocus (true);

        addTrack();
        updateDevice();
        setSize (1440, 860);
        startTimerHz (30);
    }

    ~MainComponent() override
    {
        stopTimer();
        engine.stop();
        PluginWindows::closeAll();
        if (auto xml = deviceManager.createStateXml())
            Engine::appDataDir().getChildFile ("audio.xml").replaceWithText (xml->toString());
        engine.saveKnownPlugins();
        engine.knownPlugins.removeChangeListener (this);
        deviceManager.removeChangeListener (this);
        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
    }

    void paint (Graphics& g) override { g.fillAll (Theme::bg); }

    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds (r.removeFromTop (64));
        auto left = r.removeFromLeft (300);
        master.setBounds (left.removeFromRight (80));
        strip.setBounds (left);
        arrange.setBounds (r);
    }

    //==========================================================================
    // Menús
    StringArray getMenuBarNames() override { return { "Archivo", "Edición", "Pista", "Transporte", "Audio" }; }

    PopupMenu getMenuForIndex (int index, const String&) override
    {
        PopupMenu m;
        auto add = [&] (int id) { m.addCommandItem (&commandManager, id); };
        switch (index)
        {
            case 0: add (Cmd::newSession); add (Cmd::openSession); add (Cmd::saveSession); add (Cmd::saveSessionAs);
                    m.addSeparator(); add (Cmd::importAudio); add (Cmd::exportMix); break;
            case 1: add (Cmd::split); add (Cmd::duplicate); add (Cmd::del); break;
            case 2: add (Cmd::addTrack); add (Cmd::removeTrack); break;
            case 3: add (Cmd::play); add (Cmd::record); add (Cmd::rewind); add (Cmd::metronome); break;
            case 4: add (Cmd::audioPrefs); add (Cmd::plugins); break;
        }
        return m;
    }
    void menuItemSelected (int, int) override {}

    //==========================================================================
    // Comandos y atajos
    ApplicationCommandTarget* getNextCommandTarget() override { return findFirstTargetParentComponent(); }

    void getAllCommands (Array<CommandID>& c) override
    {
        const CommandID ids[] = { Cmd::newSession, Cmd::openSession, Cmd::saveSession, Cmd::saveSessionAs, Cmd::importAudio,
                                  Cmd::exportMix, Cmd::addTrack, Cmd::removeTrack, Cmd::split, Cmd::del, Cmd::duplicate,
                                  Cmd::play, Cmd::record, Cmd::rewind, Cmd::metronome, Cmd::audioPrefs, Cmd::plugins };
        c.addArray (ids, numElementsInArray (ids));
    }

    void getCommandInfo (CommandID id, ApplicationCommandInfo& i) override
    {
        const auto cmd = ModifierKeys::commandModifier;
        const auto shift = ModifierKeys::shiftModifier;
        switch (id)
        {
            case Cmd::newSession:     i.setInfo ("Nueva sesión", "", "Archivo", 0); i.addDefaultKeypress ('n', cmd); break;
            case Cmd::openSession:    i.setInfo ("Abrir sesión…", "", "Archivo", 0); i.addDefaultKeypress ('o', cmd); break;
            case Cmd::saveSession:    i.setInfo ("Guardar", "", "Archivo", 0); i.addDefaultKeypress ('s', cmd); break;
            case Cmd::saveSessionAs:  i.setInfo ("Guardar como…", "", "Archivo", 0); i.addDefaultKeypress ('s', cmd | shift); break;
            case Cmd::importAudio:    i.setInfo ("Importar audio…", "", "Archivo", 0); i.addDefaultKeypress ('i', cmd); break;
            case Cmd::exportMix:      i.setInfo ("Exportar mezcla…", "", "Archivo", 0); i.addDefaultKeypress ('e', cmd | shift); break;
            case Cmd::addTrack:       i.setInfo ("Nueva pista de audio", "", "Pista", 0); i.addDefaultKeypress ('t', cmd); break;
            case Cmd::removeTrack:    i.setInfo ("Eliminar pista", "", "Pista", 0); break;
            case Cmd::split:          i.setInfo ("Dividir región en el cursor", "", "Edición", 0); i.addDefaultKeypress ('e', cmd); break;
            case Cmd::del:            i.setInfo ("Eliminar región", "", "Edición", 0);
                                      i.addDefaultKeypress (KeyPress::backspaceKey, 0); i.addDefaultKeypress (KeyPress::deleteKey, 0); break;
            case Cmd::duplicate:      i.setInfo ("Duplicar región", "", "Edición", 0); i.addDefaultKeypress ('d', cmd); break;
            case Cmd::play:           i.setInfo ("Reproducir / Detener", "", "Transporte", 0); i.addDefaultKeypress (KeyPress::spaceKey, 0); break;
            case Cmd::record:         i.setInfo ("Grabar", "", "Transporte", 0); i.addDefaultKeypress ('r', 0); break;
            case Cmd::rewind:         i.setInfo ("Ir al inicio", "", "Transporte", 0); i.addDefaultKeypress (KeyPress::returnKey, 0); break;
            case Cmd::metronome:      i.setInfo ("Metrónomo", "", "Transporte", 0); i.addDefaultKeypress ('k', 0); break;
            case Cmd::audioPrefs:     i.setInfo ("Preferencias de audio…", "", "Audio", 0); i.addDefaultKeypress (',', cmd); break;
            case Cmd::plugins:        i.setInfo ("Administrar plugins…", "", "Audio", 0); i.addDefaultKeypress ('p', cmd | shift); break;
            default: break;
        }
    }

    bool perform (const InvocationInfo& info) override
    {
        switch (info.commandID)
        {
            case Cmd::newSession:    newSession(); break;
            case Cmd::openSession:   openSession(); break;
            case Cmd::saveSession:   saveSession (false); break;
            case Cmd::saveSessionAs: saveSession (true); break;
            case Cmd::importAudio:   importAudio(); break;
            case Cmd::exportMix:     exportMix(); break;
            case Cmd::addTrack:      addTrack(); break;
            case Cmd::removeTrack:   removeTrack(); break;
            case Cmd::split:
                if (arrange.st.selClip) { engine.splitClip (arrange.st.selClip, engine.playhead.load()); arrange.st.selClip = nullptr; }
                break;
            case Cmd::del:
                if (arrange.st.selClip) { engine.deleteClip (arrange.st.selClip); arrange.st.selClip = nullptr; }
                break;
            case Cmd::duplicate:
                if (arrange.st.selClip) engine.duplicateClip (arrange.st.selClip);
                break;
            case Cmd::play:
                if (engine.isPlaying()) engine.stop(); else engine.play();
                break;
            case Cmd::record:
                if (engine.isRecording()) engine.stop();
                else if (! engine.startRecording())
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Grabar",
                                                      "Arma una pista con el botón R antes de grabar.");
                break;
            case Cmd::rewind:        engine.setPlayhead (0); arrange.st.scrollSec = 0; break;
            case Cmd::metronome:     engine.metronome = ! engine.metronome.load(); break;
            case Cmd::audioPrefs:    showAudioPrefs(); break;
            case Cmd::plugins:       showPluginManager(); break;
            default: return false;
        }
        return true;
    }

private:
    //==========================================================================
    void timerCallback() override { master.tick(); }

    void changeListenerCallback (ChangeBroadcaster* src) override
    {
        if (src == &engine.knownPlugins) { engine.saveKnownPlugins(); return; }
        updateDevice();
    }

    void updateDevice()
    {
        if (auto* d = deviceManager.getCurrentAudioDevice())
            engine.latencySamples = d->getInputLatencyInSamples() + d->getOutputLatencyInSamples();
        engine.refreshClipsForSampleRate();
        strip.refreshInputs();
    }

    void selectTrack (int i)
    {
        arrange.st.selTrack = i;
        strip.setTrack (isPositiveAndBelow (i, engine.numTracks()) ? &engine.track (i) : nullptr);
    }

    void addTrack()
    {
        engine.addTrack ("Audio " + String (engine.numTracks() + 1));
        arrange.rebuild();
        selectTrack (engine.numTracks() - 1);
    }

    void removeTrack()
    {
        const int i = arrange.st.selTrack;
        if (! isPositiveAndBelow (i, engine.numTracks())) return;
        engine.stop();
        PluginWindows::closeAll();
        strip.setTrack (nullptr);
        arrange.st.selClip = nullptr;
        engine.removeTrack (i);
        arrange.rebuild();
        selectTrack (jmin (i, engine.numTracks() - 1));
    }

    void rebuildAll()
    {
        arrange.st.selClip = nullptr;
        arrange.st.scrollSec = 0;
        arrange.rebuild();
        transport.setBpm (engine.bpm.load());
        selectTrack (engine.numTracks() > 0 ? 0 : -1);
    }

    //==========================================================================
    void newSession()
    {
        PluginWindows::closeAll();
        strip.setTrack (nullptr);
        engine.clearSession();
        sessionFile = File();
        addTrack();
    }

    void openSession()
    {
        chooser = std::make_unique<FileChooser> ("Abrir sesión", File::getSpecialLocation (File::userDocumentsDirectory), "*.ecaudio");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [this] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (! f.existsAsFile()) return;
                if (auto xml = parseXML (f))
                {
                    PluginWindows::closeAll();
                    strip.setTrack (nullptr);
                    if (engine.loadState (*xml)) { sessionFile = f; rebuildAll(); }
                    else AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Abrir", "Archivo de sesión no válido.");
                }
            });
    }

    void saveSession (bool saveAs)
    {
        auto doSave = [this] (const File& f)
        {
            if (auto xml = engine.saveState())
                f.withFileExtension ("ecaudio").replaceWithText (xml->toString());
            sessionFile = f.withFileExtension ("ecaudio");
        };
        if (! saveAs && sessionFile != File()) { doSave (sessionFile); return; }

        chooser = std::make_unique<FileChooser> ("Guardar sesión", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Sesión.ecaudio"), "*.ecaudio");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [doSave] (const FileChooser& fc) { auto f = fc.getResult(); if (f != File()) doSave (f); });
    }

    void importAudio()
    {
        chooser = std::make_unique<FileChooser> ("Importar audio", File::getSpecialLocation (File::userMusicDirectory),
                                                 engine.audioFormats.getWildcardForAllFormats());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
            [this] (const FileChooser& fc)
            {
                for (auto& f : fc.getResults())
                {
                    if (engine.numTracks() == 0) addTrack();
                    if (! engine.importFile (f, jmax (0, arrange.st.selTrack), engine.playhead.load()))
                        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Importar", "No se pudo leer: " + f.getFileName());
                }
            });
    }

    //==========================================================================
    static String extFor (int fmt) { return fmt == 3 ? ".aif" : fmt == 4 ? ".flac" : ".wav"; }

    struct RenderJob : public ThreadWithProgressWindow
    {
        RenderJob (Engine& e, File f, int fm) : ThreadWithProgressWindow ("Exportando mezcla…", true, true), eng (e), file (f), fmt (fm) {}
        void run() override
        {
            ok = eng.renderTo (file, fmt, [this] (float p) { setProgress (p); return ! threadShouldExit(); });
        }
        Engine& eng; File file; int fmt; bool ok = false;
    };

    void exportMix()
    {
        if (engine.projectEnd() <= 0)
        {
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon, "Exportar", "No hay audio para exportar.");
            return;
        }
        engine.stop();
        auto* aw = new AlertWindow ("Exportar mezcla", "Formato de salida (frecuencia de la sesión):", MessageBoxIconType::NoIcon);
        aw->addComboBox ("fmt", { "WAV 24-bit", "WAV 32-bit float", "WAV 16-bit (con dither)", "AIFF 24-bit", "FLAC 24-bit" });
        aw->getComboBoxComponent ("fmt")->setSelectedItemIndex (0);
        aw->addButton ("Exportar", 1, KeyPress (KeyPress::returnKey));
        aw->addButton ("Cancelar", 0, KeyPress (KeyPress::escapeKey));
        aw->enterModalState (true, ModalCallbackFunction::create ([this, aw] (int r)
        {
            if (r != 1) return;
            chooseExportFile (aw->getComboBoxComponent ("fmt")->getSelectedItemIndex());
        }), true);
    }

    void chooseExportFile (int fmt)
    {
        chooser = std::make_unique<FileChooser> ("Exportar mezcla",
                    File::getSpecialLocation (File::userMusicDirectory).getChildFile ("Mezcla" + extFor (fmt)), "*" + extFor (fmt));
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [this, fmt] (const FileChooser& fc)
            {
                auto f = fc.getResult();
                if (f == File()) return;
                f = f.withFileExtension (extFor (fmt));
                RenderJob job (engine, f, fmt);
                job.runThread();
                AlertWindow::showMessageBoxAsync (job.ok ? MessageBoxIconType::InfoIcon : MessageBoxIconType::WarningIcon, "Exportar",
                                                  job.ok ? "Mezcla exportada:\n" + f.getFullPathName() : String ("La exportación se canceló o falló."));
            });
    }

    //==========================================================================
    void showAudioPrefs()
    {
        auto* sel = new AudioDeviceSelectorComponent (deviceManager, 0, 32, 0, 32, false, false, false, false);
        sel->setSize (540, 480);
        DialogWindow::LaunchOptions o;
        o.content.setOwned (sel);
        o.dialogTitle = "Preferencias de audio";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.launchAsync();
    }

    void showPluginManager()
    {
        auto* list = new PluginListComponent (engine.pluginFormats, engine.knownPlugins,
                                              Engine::appDataDir().getChildFile ("scan-deadman.txt"), nullptr, false);
        list->setSize (760, 500);
        DialogWindow::LaunchOptions o;
        o.content.setOwned (list);
        o.dialogTitle = "Administrar plugins (VST3 / AU)";
        o.componentToCentreAround = this;
        o.dialogBackgroundColour = Theme::panel;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        o.resizable = true;
        o.launchAsync();
    }

    //==========================================================================
    Engine engine;
    AudioDeviceManager deviceManager;
    AudioProcessorPlayer player;
    ApplicationCommandManager commandManager;
    ArrangeView arrange;
    ChannelStrip strip;
    MasterStrip master;
    TransportBar transport;
    std::unique_ptr<FileChooser> chooser;
    File sessionFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

//==============================================================================
class ECApplication : public JUCEApplication
{
public:
    const String getApplicationName() override    { return "EC Audio"; }
    const String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override    { return false; }

    void initialise (const String&) override
    {
        lnf = std::make_unique<ECLookAndFeel>();
        LookAndFeel::setDefaultLookAndFeel (lnf.get());
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }
    void shutdown() override
    {
        mainWindow = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
        lnf = nullptr;
    }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public DocumentWindow
    {
    public:
        explicit MainWindow (const String& name)
            : DocumentWindow (name, Theme::bg, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* mc = new MainComponent();
            setContentOwned (mc, true);
            setResizable (true, true);
            setResizeLimits (1100, 720, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
           #if JUCE_MAC
            MenuBarModel::setMacMainMenu (mc);
           #else
            setMenuBar (mc);
           #endif
            setVisible (true);
        }
        ~MainWindow() override
        {
           #if JUCE_MAC
            MenuBarModel::setMacMainMenu (nullptr);
           #endif
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<ECLookAndFeel> lnf;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ECApplication)
