#include "Engine.h"

//==============================================================================
void Clip::buildPeaks()
{
    peaks.clear();
    const int n = data.getNumSamples();
    peaks.reserve ((size_t) (n / peakRes + 1) * 2);
    for (int s = 0; s < n; s += peakRes)
    {
        const int len = jmin (peakRes, n - s);
        float mn = 0.f, mx = 0.f;
        for (int c = 0; c < data.getNumChannels(); ++c)
        {
            auto r = data.findMinMax (c, s, len);
            mn = jmin (mn, r.getStart());
            mx = jmax (mx, r.getEnd());
        }
        peaks.push_back (mn);
        peaks.push_back (mx);
    }
}

//==============================================================================
void NativeEQ::prepare (double s)
{
    sr = s;
    cLow = cMid = cHigh = cFreq = -999.f;
    for (int c = 0; c < 2; ++c) { low[c].reset(); mid[c].reset(); high[c].reset(); }
}

void NativeEQ::process (AudioBuffer<float>& b)
{
    const float l = lowDb.load(), m = midDb.load(), h = highDb.load(), f = midFreq.load();
    if (l != cLow || m != cMid || h != cHigh || f != cFreq)
    {
        cLow = l; cMid = m; cHigh = h; cFreq = f;
        auto lc = IIRCoefficients::makeLowShelf  (sr, 120.0,  0.707, Decibels::decibelsToGain (l));
        auto mc = IIRCoefficients::makePeakFilter (sr, (double) f, 1.0, Decibels::decibelsToGain (m));
        auto hc = IIRCoefficients::makeHighShelf (sr, 8000.0, 0.707, Decibels::decibelsToGain (h));
        for (int c = 0; c < 2; ++c) { low[c].setCoefficients (lc); mid[c].setCoefficients (mc); high[c].setCoefficients (hc); }
    }
    const int n = b.getNumSamples();
    for (int c = 0; c < jmin (2, b.getNumChannels()); ++c)
    {
        auto* d = b.getWritePointer (c);
        low[c].processSamples (d, n);
        mid[c].processSamples (d, n);
        high[c].processSamples (d, n);
    }
}

//==============================================================================
void NativeComp::prepare (double sr, int maxBlock)
{
    comp.prepare ({ sr, (uint32) maxBlock, 2 });
    comp.reset();
}

void NativeComp::process (AudioBuffer<float>& b)
{
    comp.setThreshold (thresholdDb.load());
    comp.setRatio     (ratio.load());
    comp.setAttack    (attackMs.load());
    comp.setRelease   (releaseMs.load());
    dsp::AudioBlock<float> blk (b);
    dsp::ProcessContextReplacing<float> ctx (blk);
    comp.process (ctx);
    b.applyGain (Decibels::decibelsToGain (makeupDb.load()));
}

//==============================================================================
File Engine::appDataDir()
{
    auto d = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("EC Audio");
    d.createDirectory();
    return d;
}
File Engine::recordingsDir()
{
    auto d = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("EC Audio").getChildFile ("Recordings");
    d.createDirectory();
    return d;
}
File Engine::editsDir()
{
    auto d = File::getSpecialLocation (File::userMusicDirectory).getChildFile ("EC Audio").getChildFile ("Edits");
    d.createDirectory();
    return d;
}

Engine::Engine()
    : AudioProcessor (BusesProperties().withInput  ("Input",  AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      writerThread ("EC Audio Disk Writer")
{
    audioFormats.registerBasicFormats();
    pluginFormats.addDefaultFormats();     // VST3 (+ AudioUnit) en macOS
    writerThread.startThread();
    inputCopy.setSize (2, maxBlock);
    masterBuf.setSize (2, maxBlock);

    if (auto xml = parseXML (appDataDir().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);
}

Engine::~Engine()
{
    stop();
    writerThread.stopThread (2000);
}

void Engine::saveKnownPlugins()
{
    if (auto xml = knownPlugins.createXml())
        appDataDir().getChildFile ("plugins.xml").replaceWithText (xml->toString());
}

//==============================================================================
void Engine::prepareTrack (Track& t)
{
    t.mix.setSize (2, maxBlock);
    t.eq.prepare (sampleRate);
    t.comp.prepare (sampleRate, maxBlock);
    for (auto& p : t.plugins)
        if (p != nullptr) { p->releaseResources(); p->prepareToPlay (sampleRate, maxBlock); }
}

void Engine::prepareToPlay (double sr, int block)
{
    ScopedLock sl (processLock);
    sampleRate = sr;
    maxBlock = jmax (64, block);
    inputCopy.setSize (jmax (2, getTotalNumInputChannels()), maxBlock);
    masterBuf.setSize (2, maxBlock);
    limiter.prepare ({ sr, (uint32) maxBlock, 2 });
    limiter.setThreshold (-0.3f);
    limiter.setRelease (100.0f);
    for (auto* t : tracks) prepareTrack (*t);
}

//==============================================================================
void Engine::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    ScopedTryLock lock (processLock);
    if (! lock.isLocked()) { buffer.clear(); return; }

    const int total = buffer.getNumSamples();
    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = jmin (maxBlock, total - pos);
        AudioBuffer<float> sub (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        processChunk (sub);
    }
}

void Engine::addClick (AudioBuffer<float>& b, int64 pos, int n)
{
    const double beat = 60.0 / bpm.load() * sampleRate;
    const double clickLen = 0.02 * sampleRate;
    for (int i = 0; i < n; ++i)
    {
        const double p = (double) (pos + i);
        const double within = std::fmod (p, beat);
        if (within < clickLen)
        {
            const auto beatIdx = (int64) (p / beat);
            const float f = (beatIdx % 4 == 0) ? 1600.f : 1000.f;
            const float env = 1.0f - (float) (within / clickLen);
            const float s = 0.35f * env * std::sin (MathConstants<float>::twoPi * f * (float) (within / sampleRate));
            b.addSample (0, i, s);
            b.addSample (1, i, s);
        }
    }
}

void Engine::processChunk (AudioBuffer<float>& io)
{
    const int n = io.getNumSamples();
    const int outCh = io.getNumChannels();
    const int numIn = offline ? 0 : jmin (getTotalNumInputChannels(), inputCopy.getNumChannels(), outCh);

    for (int c = 0; c < numIn; ++c)
        inputCopy.copyFrom (c, 0, io, c, 0, n);
    io.clear();

    const bool isPlaying = playing.load();
    const int64 pos = playhead.load();

    bool anySolo = false;
    for (auto* t : tracks) if (t->solo.load()) anySolo = true;

    AudioBuffer<float> mb (masterBuf.getArrayOfWritePointers(), 2, n);
    mb.clear();

    for (auto* tp : tracks)
    {
        auto& t = *tp;
        AudioBuffer<float> mix (t.mix.getArrayOfWritePointers(), 2, n);
        mix.clear();

        // --- reproducción de clips
        if (isPlaying)
        {
            for (auto& c : t.clips)
            {
                const int64 cs = c->start, ce = cs + c->length();
                if (ce <= pos || cs >= pos + n) continue;
                const int from = (int) jmax<int64> (0, cs - pos);
                const int to   = (int) jmin<int64> (n, ce - pos);
                const int srcStart = (int) (pos + from - cs);
                const int num = to - from;
                for (int ch = 0; ch < 2; ++ch)
                    mix.addFrom (ch, from, c->data, jmin (ch, c->data.getNumChannels() - 1), srcStart, num);
            }
        }

        // --- entrada en vivo: monitor de voz en tiempo real + grabación
        const int inCh = t.inputChannel.load();
        if (! offline && t.armed.load() && inCh < numIn)
        {
            const float* in = inputCopy.getReadPointer (inCh);
            if (t.monitor.load())
                for (int ch = 0; ch < 2; ++ch) mix.addFrom (ch, 0, in, n);
            if (t.writer != nullptr)
                t.writer->write (&in, n);
        }

        // --- procesamiento: EQ -> compresor -> 10 inserts VST3
        if (t.eq.enabled.load())   t.eq.process (mix);
        if (t.comp.enabled.load()) t.comp.process (mix);
        for (int i = 0; i < kMaxInserts; ++i)
            if (auto* p = t.plugins[(size_t) i].get(); p != nullptr && ! t.bypass[(size_t) i].load())
            {
                emptyMidi.clear();
                p->processBlock (mix, emptyMidi);
            }

        // --- volumen / pan / mute / solo
        float g = Decibels::decibelsToGain (t.gainDb.load(), -59.0f);
        if (t.mute.load() || (anySolo && ! t.solo.load())) g = 0.f;
        const float angle = (t.pan.load() + 1.0f) * 0.25f * MathConstants<float>::pi;
        mix.applyGain (0, 0, n, std::cos (angle) * g * 1.41421356f);
        mix.applyGain (1, 0, n, std::sin (angle) * g * 1.41421356f);

        const float pk = jmax (mix.getMagnitude (0, 0, n), mix.getMagnitude (1, 0, n));
        t.level.store (jmax (pk, t.level.load() * 0.85f));

        mb.addFrom (0, 0, mix, 0, 0, n);
        mb.addFrom (1, 0, mix, 1, 0, n);
    }

    if (isPlaying && metronome.load() && ! offline) addClick (mb, pos, n);

    mb.applyGain (Decibels::decibelsToGain (masterDb.load(), -59.0f));
    {
        dsp::AudioBlock<float> blk (mb);
        dsp::ProcessContextReplacing<float> ctx (blk);
        limiter.process (ctx);
    }
    const float mpk = jmax (mb.getMagnitude (0, 0, n), mb.getMagnitude (1, 0, n));
    masterLevel.store (jmax (mpk, masterLevel.load() * 0.85f));

    if (outCh == 1)
    {
        io.copyFrom (0, 0, mb, 0, 0, n);
        io.addFrom  (0, 0, mb, 1, 0, n);
        io.applyGain (0.5f);
    }
    else
        for (int c = 0; c < jmin (2, outCh); ++c) io.copyFrom (c, 0, mb, c, 0, n);

    if (isPlaying) playhead.store (pos + n);
}

//==============================================================================
Track* Engine::addTrack (const String& name)
{
    auto t = std::make_unique<Track>();
    t->name = name;
    prepareTrack (*t);
    auto* raw = t.get();
    ScopedLock sl (processLock);
    tracks.add (t.release());
    return raw;
}

void Engine::removeTrack (int index)
{
    std::unique_ptr<Track> old;
    {
        ScopedLock sl (processLock);
        if (! isPositiveAndBelow (index, tracks.size())) return;
        old.reset (tracks.removeAndReturn (index));
    }
}

void Engine::clearSession()
{
    stop();
    OwnedArray<Track> old;
    {
        ScopedLock sl (processLock);
        old.swapWith (tracks);
        playhead = 0;
    }
}

void Engine::stop()
{
    const bool wasRecording = recording.exchange (false);
    playing = false;
    if (wasRecording) finishRecording();
}

bool Engine::startRecording()
{
    if (recording.load()) return true;
    const auto dir = recordingsDir();
    WavAudioFormat wav;
    int armed = 0;

    for (auto* t : tracks)
    {
        if (! t->armed.load()) continue;
        auto f = dir.getNonexistentChildFile (File::createLegalFileName (t->name) + "_" + Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S"), ".wav", false);
        std::unique_ptr<FileOutputStream> fs (f.createOutputStream());
        if (fs == nullptr) continue;
        // 24 bits, mono, a la frecuencia del dispositivo (sin conversión)
        std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (fs.get(), sampleRate, 1, 24, {}, 0));
        if (w == nullptr) continue;
        fs.release();

        auto tw = std::make_unique<AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 65536);
        ScopedLock sl (processLock);
        t->recFile = f;
        t->recStart = playhead.load();
        t->writer = std::move (tw);
        ++armed;
    }
    if (armed == 0) return false;
    recording = true;
    playing = true;
    return true;
}

void Engine::finishRecording()
{
    struct Done { std::unique_ptr<AudioFormatWriter::ThreadedWriter> w; File f; int64 start; Track* t; };
    std::vector<Done> done;
    {
        ScopedLock sl (processLock);
        for (auto* t : tracks)
            if (t->writer != nullptr)
                done.push_back (Done { std::move (t->writer), t->recFile, t->recStart, t });
    }
    for (auto& d : done) d.w.reset();       // vacía el búfer y cierra el WAV

    for (auto& d : done)
        if (auto clip = loadClip (d.f))
        {
            // compensación de latencia (entrada + salida)
            clip->start = jmax<int64> (0, d.start - latencySamples.load());
            ScopedLock sl (processLock);
            d.t->clips.push_back (clip);
        }
}

//==============================================================================
std::shared_ptr<Clip> Engine::loadClip (const File& f)
{
    std::unique_ptr<AudioFormatReader> r (audioFormats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0) return nullptr;

    auto clip = std::make_shared<Clip>();
    clip->file = f;
    clip->name = f.getFileNameWithoutExtension();
    clip->rate = sampleRate;

    const int nch = (int) jmin<unsigned int> (r->numChannels, 2u);
    const int len = (int) r->lengthInSamples;
    AudioBuffer<float> raw (nch, len + 8);
    raw.clear();
    r->read (&raw, 0, len, 0, true, nch > 1);

    if (std::abs (r->sampleRate - sampleRate) > 1.0)
    {
        const double ratio = r->sampleRate / sampleRate;
        const int outLen = (int) std::ceil (len / ratio);
        AudioBuffer<float> out (nch, outLen);
        for (int c = 0; c < nch; ++c)
        {
            LagrangeInterpolator li;
            li.process (ratio, raw.getReadPointer (c), out.getWritePointer (c), outLen);
        }
        clip->data = std::move (out);
    }
    else
    {
        raw.setSize (nch, len, true);
        clip->data = std::move (raw);
    }
    clip->buildPeaks();
    return clip;
}

bool Engine::importFile (const File& f, int trackIndex, int64 at)
{
    auto clip = loadClip (f);
    if (clip == nullptr || ! isPositiveAndBelow (trackIndex, tracks.size())) return false;
    clip->start = at;
    ScopedLock sl (processLock);
    tracks[trackIndex]->clips.push_back (clip);
    return true;
}

void Engine::refreshClipsForSampleRate()
{
    for (auto* t : tracks)
        for (auto& c : t->clips)
            if (std::abs (c->rate - sampleRate) > 1.0)
                if (auto n = loadClip (c->file))
                {
                    ScopedLock sl (processLock);
                    c->start = (int64) ((double) c->start * sampleRate / c->rate);
                    c->data = std::move (n->data);
                    c->peaks = std::move (n->peaks);
                    c->rate = sampleRate;
                }
}

int64 Engine::projectEnd() const
{
    int64 e = 0;
    for (auto* t : tracks) for (auto& c : t->clips) e = jmax (e, c->start + c->length());
    return e;
}

int Engine::trackIndexOf (const std::shared_ptr<Clip>& c) const
{
    for (int i = 0; i < tracks.size(); ++i)
        for (auto& x : tracks[i]->clips) if (x == c) return i;
    return -1;
}

bool Engine::writeWav32 (const File& f, const AudioBuffer<float>& b, double rate)
{
    WavAudioFormat wav;
    std::unique_ptr<FileOutputStream> fs (f.createOutputStream());
    if (fs == nullptr) return false;
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (fs.get(), rate, (unsigned int) b.getNumChannels(), 32, {}, 0));
    if (w == nullptr) return false;
    fs.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

std::shared_ptr<Clip> Engine::makeSubClip (const Clip& src, int from, int num, int64 timelineStart)
{
    auto c = std::make_shared<Clip>();
    c->name = src.name;
    c->rate = src.rate;
    c->start = timelineStart;
    const int nch = src.data.getNumChannels();
    c->data.setSize (nch, num);
    for (int ch = 0; ch < nch; ++ch) c->data.copyFrom (ch, 0, src.data, ch, from, num);
    c->file = editsDir().getNonexistentChildFile (File::createLegalFileName (src.name), ".wav", false);
    { auto w = c->data; writeWav32 (c->file, w, c->rate); }
    c->buildPeaks();
    return c;
}

bool Engine::splitClip (std::shared_ptr<Clip> c, int64 at)
{
    const int ti = trackIndexOf (c);
    if (ti < 0) return false;
    const int64 off = at - c->start;
    if (off <= 0 || off >= c->length()) return false;

    auto a = makeSubClip (*c, 0, (int) off, c->start);
    auto b = makeSubClip (*c, (int) off, (int) (c->length() - off), at);

    ScopedLock sl (processLock);
    auto& v = tracks[ti]->clips;
    auto it = std::find (v.begin(), v.end(), c);
    if (it == v.end()) return false;
    *it = a;
    v.insert (it + 1, b);
    return true;
}

void Engine::deleteClip (std::shared_ptr<Clip> c)
{
    const int ti = trackIndexOf (c);
    if (ti < 0) return;
    ScopedLock sl (processLock);
    auto& v = tracks[ti]->clips;
    v.erase (std::remove (v.begin(), v.end(), c), v.end());
}

void Engine::duplicateClip (std::shared_ptr<Clip> c)
{
    const int ti = trackIndexOf (c);
    if (ti < 0) return;
    auto d = std::make_shared<Clip> (*c);
    d->start = c->start + c->length();
    ScopedLock sl (processLock);
    tracks[ti]->clips.push_back (d);
}

//==============================================================================
bool Engine::loadPlugin (Track& t, int slot, const PluginDescription& d, String& err, const String& state)
{
    slot = jlimit (0, kMaxInserts - 1, slot);
    auto inst = pluginFormats.createPluginInstance (d, sampleRate, maxBlock, err);
    if (inst == nullptr) return false;

    AudioProcessor::BusesLayout layout;
    for (int i = 0; i < inst->getBusCount (true); ++i)  layout.inputBuses.add  (i == 0 ? AudioChannelSet::stereo() : AudioChannelSet::disabled());
    for (int i = 0; i < inst->getBusCount (false); ++i) layout.outputBuses.add (i == 0 ? AudioChannelSet::stereo() : AudioChannelSet::disabled());
    if (inst->checkBusesLayoutSupported (layout)) inst->setBusesLayout (layout);

    if (state.isNotEmpty())
    {
        MemoryBlock mb;
        if (mb.fromBase64Encoding (state)) inst->setStateInformation (mb.getData(), (int) mb.getSize());
    }
    inst->prepareToPlay (sampleRate, maxBlock);

    std::unique_ptr<AudioPluginInstance> old;
    {
        ScopedLock sl (processLock);
        old = std::move (t.plugins[(size_t) slot]);
        t.plugins[(size_t) slot] = std::move (inst);
        t.bypass[(size_t) slot] = false;
    }
    return true;
}

void Engine::clearPlugin (Track& t, int slot)
{
    std::unique_ptr<AudioPluginInstance> old;
    ScopedLock sl (processLock);
    old = std::move (t.plugins[(size_t) jlimit (0, kMaxInserts - 1, slot)]);
}

//==============================================================================
std::unique_ptr<XmlElement> Engine::saveState() const
{
    auto x = std::make_unique<XmlElement> ("ECSession");
    x->setAttribute ("bpm", bpm.load());
    x->setAttribute ("master", (double) masterDb.load());
    x->setAttribute ("metro", metronome.load() ? 1 : 0);
    x->setAttribute ("sr", sampleRate);

    for (auto* t : tracks)
    {
        auto* e = x->createNewChildElement ("Track");
        e->setAttribute ("name", t->name);
        e->setAttribute ("gain", (double) t->gainDb.load());
        e->setAttribute ("pan", (double) t->pan.load());
        e->setAttribute ("mute", t->mute.load() ? 1 : 0);
        e->setAttribute ("solo", t->solo.load() ? 1 : 0);
        e->setAttribute ("input", t->inputChannel.load());
        e->setAttribute ("eqOn", t->eq.enabled.load() ? 1 : 0);
        e->setAttribute ("eqLow", (double) t->eq.lowDb.load());
        e->setAttribute ("eqMid", (double) t->eq.midDb.load());
        e->setAttribute ("eqHigh", (double) t->eq.highDb.load());
        e->setAttribute ("eqFreq", (double) t->eq.midFreq.load());
        e->setAttribute ("cpOn", t->comp.enabled.load() ? 1 : 0);
        e->setAttribute ("cpThr", (double) t->comp.thresholdDb.load());
        e->setAttribute ("cpRatio", (double) t->comp.ratio.load());
        e->setAttribute ("cpAtk", (double) t->comp.attackMs.load());
        e->setAttribute ("cpRel", (double) t->comp.releaseMs.load());
        e->setAttribute ("cpMk", (double) t->comp.makeupDb.load());

        for (auto& c : t->clips)
        {
            auto* ce = e->createNewChildElement ("Clip");
            ce->setAttribute ("file", c->file.getFullPathName());
            ce->setAttribute ("name", c->name);
            ce->setAttribute ("start", String (c->start));
        }
        for (int i = 0; i < kMaxInserts; ++i)
            if (auto* p = t->plugins[(size_t) i].get())
            {
                auto* pe = e->createNewChildElement ("Plugin");
                pe->setAttribute ("slot", i);
                pe->setAttribute ("bypass", t->bypass[(size_t) i].load() ? 1 : 0);
                pe->addChildElement (p->getPluginDescription().createXml().release());
                MemoryBlock mb;
                p->getStateInformation (mb);
                pe->setAttribute ("state", mb.toBase64Encoding());
            }
    }
    return x;
}

bool Engine::loadState (const XmlElement& x)
{
    if (! x.hasTagName ("ECSession")) return false;
    clearSession();
    bpm = x.getDoubleAttribute ("bpm", 120.0);
    masterDb = (float) x.getDoubleAttribute ("master", 0.0);
    metronome = x.getBoolAttribute ("metro", false);
    const double fileRate = x.getDoubleAttribute ("sr", sampleRate);

    for (auto* e : x.getChildWithTagNameIterator ("Track"))
    {
        auto* t = addTrack (e->getStringAttribute ("name", "Audio"));
        t->gainDb = (float) e->getDoubleAttribute ("gain");
        t->pan = (float) e->getDoubleAttribute ("pan");
        t->mute = e->getBoolAttribute ("mute");
        t->solo = e->getBoolAttribute ("solo");
        t->inputChannel = e->getIntAttribute ("input");
        t->eq.enabled = e->getBoolAttribute ("eqOn", true);
        t->eq.lowDb = (float) e->getDoubleAttribute ("eqLow");
        t->eq.midDb = (float) e->getDoubleAttribute ("eqMid");
        t->eq.highDb = (float) e->getDoubleAttribute ("eqHigh");
        t->eq.midFreq = (float) e->getDoubleAttribute ("eqFreq", 1000.0);
        t->comp.enabled = e->getBoolAttribute ("cpOn");
        t->comp.thresholdDb = (float) e->getDoubleAttribute ("cpThr", -18.0);
        t->comp.ratio = (float) e->getDoubleAttribute ("cpRatio", 3.0);
        t->comp.attackMs = (float) e->getDoubleAttribute ("cpAtk", 10.0);
        t->comp.releaseMs = (float) e->getDoubleAttribute ("cpRel", 120.0);
        t->comp.makeupDb = (float) e->getDoubleAttribute ("cpMk");

        for (auto* ce : e->getChildWithTagNameIterator ("Clip"))
        {
            File f (ce->getStringAttribute ("file"));
            if (! f.existsAsFile()) continue;
            if (auto clip = loadClip (f))
            {
                clip->name = ce->getStringAttribute ("name", clip->name);
                clip->start = (int64) ((double) ce->getStringAttribute ("start").getLargeIntValue() * sampleRate / fileRate);
                t->clips.push_back (clip);
            }
        }
        for (auto* pe : e->getChildWithTagNameIterator ("Plugin"))
        {
            PluginDescription d;
            if (auto* de = pe->getFirstChildElement(); de != nullptr && d.loadFromXml (*de))
            {
                String err;
                const int slot = jlimit (0, kMaxInserts - 1, pe->getIntAttribute ("slot"));
                if (loadPlugin (*t, slot, d, err, pe->getStringAttribute ("state")))
                    t->bypass[(size_t) slot] = pe->getBoolAttribute ("bypass");
            }
        }
    }
    return true;
}

//==============================================================================
bool Engine::renderTo (const File& file, int format, std::function<bool (float)> progress)
{
    int64 end = projectEnd();
    if (end <= 0) return false;
    end += (int64) (sampleRate * 2.0);   // cola para reverbs / delays

    std::unique_ptr<AudioFormat> fmt;
    int bits = 24;
    switch (format)
    {
        case 0:  fmt = std::make_unique<WavAudioFormat>();  bits = 24; break;
        case 1:  fmt = std::make_unique<WavAudioFormat>();  bits = 32; break;
        case 2:  fmt = std::make_unique<WavAudioFormat>();  bits = 16; break;
        case 3:  fmt = std::make_unique<AiffAudioFormat>(); bits = 24; break;
        default: fmt = std::make_unique<FlacAudioFormat>(); bits = 24; break;
    }

    file.deleteFile();
    std::unique_ptr<FileOutputStream> fs (file.createOutputStream());
    if (fs == nullptr) return false;
    std::unique_ptr<AudioFormatWriter> w (fmt->createWriterFor (fs.get(), sampleRate, 2, bits, {}, 0));
    if (w == nullptr) return false;
    fs.release();

    ScopedLock sl (processLock);       // el hilo de audio queda en silencio durante la exportación
    const bool wasPlaying = playing.load();
    const int64 oldPos = playhead.load();
    offline = true; playing = true; playhead = 0;
    limiter.reset();
    for (auto* t : tracks) { t->eq.prepare (sampleRate); t->comp.prepare (sampleRate, maxBlock); }

    const int blk = jmin (maxBlock, 1024);
    AudioBuffer<float> buf (2, blk);
    Random rng;
    bool ok = true;

    for (int64 done = 0; done < end; done += blk)
    {
        const int n = (int) jmin<int64> (blk, end - done);
        AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
        processChunk (view);
        if (bits == 16)   // dither TPDF
            for (int c = 0; c < 2; ++c)
            {
                auto* d = view.getWritePointer (c);
                for (int i = 0; i < n; ++i) d[i] += (rng.nextFloat() - rng.nextFloat()) / 32768.0f;
            }
        w->writeFromAudioSampleBuffer (view, 0, n);
        if (progress && ! progress ((float) ((double) done / (double) end))) { ok = false; break; }
    }

    offline = false; playing = wasPlaying; playhead = oldPos;
    for (auto* t : tracks) { t->eq.prepare (sampleRate); t->comp.prepare (sampleRate, maxBlock); }
    w.reset();
    if (! ok) file.deleteFile();
    return ok;
}
