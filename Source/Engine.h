#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

using namespace juce;

constexpr int kMaxInserts = 10;   // plugins VST3 por pista

//==============================================================================
struct Clip
{
    static constexpr int peakRes = 256;
    String name;
    File file;
    AudioBuffer<float> data;      // a la frecuencia de muestreo del motor
    double rate = 44100.0;
    int64 start = 0;              // posición en la línea de tiempo (muestras)
    std::vector<float> peaks;     // min/max intercalado cada peakRes muestras

    int64 length() const { return data.getNumSamples(); }
    void buildPeaks();
};

//==============================================================================
// Ecualizador nativo: graves (shelf 120 Hz), medios (campana variable), agudos (shelf 8 kHz)
struct NativeEQ
{
    std::atomic<float> lowDb { 0.f }, midDb { 0.f }, highDb { 0.f }, midFreq { 1000.f };
    std::atomic<bool> enabled { true };

    void prepare (double sampleRate);
    void process (AudioBuffer<float>& b);   // estéreo

private:
    double sr = 44100.0;
    float cLow = -999.f, cMid = -999.f, cHigh = -999.f, cFreq = -999.f;
    IIRFilter low[2], mid[2], high[2];
};

// Compresor nativo
struct NativeComp
{
    std::atomic<float> thresholdDb { -18.f }, ratio { 3.f }, attackMs { 10.f }, releaseMs { 120.f }, makeupDb { 0.f };
    std::atomic<bool> enabled { false };

    void prepare (double sampleRate, int maxBlock);
    void process (AudioBuffer<float>& b);   // estéreo

private:
    dsp::Compressor<float> comp;
};

//==============================================================================
struct Track
{
    String name { "Audio" };
    std::atomic<float> gainDb { 0.f }, pan { 0.f }, level { 0.f };
    std::atomic<bool> mute { false }, solo { false }, armed { false }, monitor { false };
    std::atomic<int> inputChannel { 0 };

    NativeEQ eq;
    NativeComp comp;
    std::array<std::unique_ptr<AudioPluginInstance>, kMaxInserts> plugins;
    std::array<std::atomic<bool>, kMaxInserts> bypass;

    std::vector<std::shared_ptr<Clip>> clips;

    AudioBuffer<float> mix;
    std::unique_ptr<AudioFormatWriter::ThreadedWriter> writer;
    File recFile;
    int64 recStart = 0;

    Track() { for (auto& b : bypass) b = false; }
};

//==============================================================================
class Engine : public AudioProcessor
{
public:
    Engine();
    ~Engine() override;

    // ---- AudioProcessor
    const String getName() const override { return "EC Audio Engine"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // ---- Pistas
    Track* addTrack (const String& name);
    void removeTrack (int index);
    int numTracks() const { return tracks.size(); }
    Track& track (int i) { return *tracks[i]; }
    void clearSession();

    // ---- Transporte
    void play()            { playing = true; }
    void stop();
    bool startRecording();               // false si no hay pistas armadas
    void setPlayhead (int64 s)           { playhead = jmax<int64> (0, s); }
    bool isPlaying() const               { return playing.load(); }
    bool isRecording() const             { return recording.load(); }

    // ---- Edición
    bool importFile (const File& f, int trackIndex, int64 at);
    bool splitClip (std::shared_ptr<Clip> c, int64 at);
    void deleteClip (std::shared_ptr<Clip> c);
    void duplicateClip (std::shared_ptr<Clip> c);
    int  trackIndexOf (const std::shared_ptr<Clip>& c) const;
    int64 projectEnd() const;
    void refreshClipsForSampleRate();

    // ---- Plugins (hasta 10 por pista)
    bool loadPlugin (Track& t, int slot, const PluginDescription& d, String& err, const String& stateBase64 = {});
    void clearPlugin (Track& t, int slot);

    // ---- Sesión / exportación
    std::unique_ptr<XmlElement> saveState() const;
    bool loadState (const XmlElement& xml);
    // format: 0 WAV24, 1 WAV32f, 2 WAV16(dither), 3 AIFF24, 4 FLAC24
    bool renderTo (const File& file, int format, std::function<bool (float)> progress);

    // ---- Carpetas
    static File appDataDir();
    static File recordingsDir();
    static File editsDir();
    void saveKnownPlugins();

    double sr() const { return sampleRate; }

    // Estado compartido con la interfaz
    std::atomic<bool>   metronome { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<float>  masterDb { 0.f }, masterLevel { 0.f };
    std::atomic<int>    latencySamples { 0 };
    std::atomic<int64>  playhead { 0 };

    AudioFormatManager audioFormats;
    AudioPluginFormatManager pluginFormats;
    KnownPluginList knownPlugins;
    CriticalSection processLock;

private:
    void processChunk (AudioBuffer<float>& io);
    void addClick (AudioBuffer<float>& b, int64 pos, int n);
    void prepareTrack (Track& t);
    void finishRecording();
    std::shared_ptr<Clip> loadClip (const File& f);
    std::shared_ptr<Clip> makeSubClip (const Clip& src, int from, int num, int64 timelineStart);
    bool writeWav32 (const File& f, const AudioBuffer<float>& b, double rate);

    OwnedArray<Track> tracks;
    TimeSliceThread writerThread;
    AudioBuffer<float> inputCopy, masterBuf;
    MidiBuffer emptyMidi;
    dsp::Limiter<float> limiter;

    double sampleRate = 44100.0;
    int maxBlock = 512;
    bool offline = false;
    std::atomic<bool> playing { false }, recording { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Engine)
};
