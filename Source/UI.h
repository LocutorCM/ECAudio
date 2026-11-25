#pragma once
#include <JuceHeader.h>
#include "Engine.h"
#include "Theme.h"

namespace Cmd
{
    enum { newSession = 0x2001, openSession, saveSession, saveSessionAs, importAudio, exportMix,
           addTrack, removeTrack, split, del, duplicate, play, record, rewind, metronome,
           audioPrefs, plugins };
}

//==============================================================================
struct IconButton : public Button
{
    enum Kind { Rewind, Stop, Play, Rec, Metro };
    explicit IconButton (Kind k) : Button ("icon"), kind (k) {}
    Kind kind;
    bool lit = false;

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (down ? Theme::panel2.brighter (0.25f) : over ? Theme::panel2.brighter (0.12f) : Theme::panel2);
        g.fillRoundedRectangle (r, 7.f);
        auto c = r.getCentre();
        const float s = r.getHeight() * 0.26f;
        g.setColour (lit ? Theme::teal : Theme::text);
        switch (kind)
        {
            case Play:   { Path p; p.addTriangle (c.x - s * 0.7f, c.y - s, c.x - s * 0.7f, c.y + s, c.x + s * 1.1f, c.y); g.fillPath (p); break; }
            case Stop:   g.fillRect (Rectangle<float> (s * 1.6f, s * 1.6f).withCentre (c)); break;
            case Rec:    g.setColour (lit ? Theme::red : Theme::red.darker (0.35f));
                         g.fillEllipse (Rectangle<float> (s * 1.8f, s * 1.8f).withCentre (c)); break;
            case Rewind: { Path p; p.addTriangle (c.x + s * 0.8f, c.y - s, c.x + s * 0.8f, c.y + s, c.x - s * 0.5f, c.y);
                           g.fillPath (p); g.fillRect (c.x - s * 0.9f, c.y - s, s * 0.35f, s * 2.f); break; }
            case Metro:  g.setFont (Font (22.f)); g.drawText (String::fromUTF8 ("\xE2\x99\xA9"), getLocalBounds(), Justification::centred); break;
        }
    }
};

struct LevelMeter : public Component
{
    std::function<float()> source;
    bool vertical = false;
    float shown = 0.f;

    void tick()
    {
        const float v = source ? source() : 0.f;
        const float db = Decibels::gainToDecibels (v, -60.f);
        const float n = jlimit (0.f, 1.f, jmap (db, -60.f, 6.f, 0.f, 1.f));
        shown = jmax (n, shown - 0.04f);
        repaint();
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Colour (0xff0a1218));
        g.fillRoundedRectangle (r, 2.f);
        auto f = vertical ? r.withTop (r.getBottom() - r.getHeight() * shown) : r.withWidth (r.getWidth() * shown);
        g.setColour (shown > 0.92f ? Theme::red : shown > 0.78f ? Colour (0xffe6c229) : Theme::teal);
        g.fillRoundedRectangle (f, 2.f);
    }
};

//==============================================================================
// Ventanas de plugins VST3
class PluginWindow : public DocumentWindow
{
public:
    PluginWindow (AudioPluginInstance& p, const String& title, std::function<void()> onCloseFn)
        : DocumentWindow (title, Theme::panel, DocumentWindow::closeButton), proc (&p), onClose (std::move (onCloseFn))
    {
        setUsingNativeTitleBar (true);
        AudioProcessorEditor* ed = p.hasEditor() ? p.createEditor() : new GenericAudioProcessorEditor (p);
        setContentOwned (ed, true);
        setResizable (ed->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    ~PluginWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { if (onClose) onClose(); }
    AudioProcessor* proc;
private:
    std::function<void()> onClose;
};

struct PluginWindows
{
    static OwnedArray<PluginWindow>& list() { static OwnedArray<PluginWindow> l; return l; }
    static void open (AudioPluginInstance& p)
    {
        for (auto* w : list()) if (w->proc == &p) { w->toFront (true); return; }
        auto* pp = &p;
        list().add (new PluginWindow (p, p.getName(), [pp] { MessageManager::callAsync ([pp] { close (pp); }); }));
    }
    static void close (AudioProcessor* p)
    {
        for (int i = list().size(); --i >= 0;) if (list()[i]->proc == p) list().remove (i);
    }
    static void closeAll() { list().clear(); }
};

//==============================================================================
struct ArrangeState
{
    static constexpr int trackH = 64;
    Engine& eng;
    double pxPerSec = 30.0, scrollSec = 0.0;
    int selTrack = 0;
    std::shared_ptr<Clip> selClip;
    std::function<void (int)> onSelectTrack;

    explicit ArrangeState (Engine& e) : eng (e) {}
    float secToX (double s) const { return (float) ((s - scrollSec) * pxPerSec); }
    double xToSec (float x) const { return (double) x / pxPerSec + scrollSec; }
    int64 xToSample (float x) const { return jmax<int64> (0, (int64) (xToSec (x) * eng.sr())); }
    void select (int i) { selTrack = i; if (onSelectTrack) onSelectTrack (i); }
};

//==============================================================================
class TrackHeader : public Component
{
public:
    TrackHeader (Track& tr, int idx, std::function<void (int)> sel) : t (tr), index (idx), onSelect (std::move (sel))
    {
        nameLabel.setText (t.name, dontSendNotification);
        nameLabel.setEditable (false, true);
        nameLabel.onTextChange = [this] { t.name = nameLabel.getText(); };
        nameLabel.addMouseListener (this, false);
        addAndMakeVisible (nameLabel);

        setup (mBtn, Colour (0xffe6c229), [this] { t.mute = mBtn.getToggleState(); });
        setup (sBtn, Colour (0xff3b9bff), [this] { t.solo = sBtn.getToggleState(); });
        setup (rBtn, Theme::red, [this]
        {
            t.armed = rBtn.getToggleState();
            t.monitor = t.armed.load();                    // monitor automático al armar
            iBtn.setToggleState (t.monitor.load(), dontSendNotification);
        });
        setup (iBtn, Theme::teal, [this] { t.monitor = iBtn.getToggleState(); });
        iBtn.setTooltip ("Monitor de entrada en tiempo real (usa audífonos)");
        mBtn.setToggleState (t.mute.load(), dontSendNotification);
        sBtn.setToggleState (t.solo.load(), dontSendNotification);
        rBtn.setToggleState (t.armed.load(), dontSendNotification);
        iBtn.setToggleState (t.monitor.load(), dontSendNotification);

        vol.setSliderStyle (Slider::LinearHorizontal);
        vol.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        vol.setRange (-60.0, 6.0);
        vol.setSkewFactorFromMidPoint (-12.0);
        vol.setDoubleClickReturnValue (true, 0.0);
        vol.setValue (t.gainDb.load(), dontSendNotification);
        vol.onValueChange = [this] { t.gainDb = (float) vol.getValue(); };
        addAndMakeVisible (vol);

        meter.source = [this] { return t.level.load(); };
        addAndMakeVisible (meter);
    }

    void tick (bool isSelected)
    {
        if (isSelected != selected) { selected = isSelected; repaint(); }
        meter.tick();
    }
    void mouseDown (const MouseEvent&) override { if (onSelect) onSelect (index); }

    void paint (Graphics& g) override
    {
        g.fillAll (selected ? Theme::panel2.brighter (0.12f) : Theme::panel2);
        g.setColour (Theme::bg);
        g.drawHorizontalLine (getHeight() - 1, 0.f, (float) getWidth());
        g.setColour (selected ? Theme::teal : Theme::dim);
        g.setFont (Font (15.f, Font::bold));
        g.drawText (String (index + 1), 4, 6, 22, 22, Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 6);
        auto top = r.removeFromTop (22);
        top.removeFromLeft (24);
        nameLabel.setBounds (top);
        r.removeFromTop (6);
        auto row = r.removeFromTop (22);
        for (auto* b : { &mBtn, &sBtn, &rBtn, &iBtn }) b->setBounds (row.removeFromLeft (30).reduced (1));
        meter.setBounds (row.removeFromRight (48).reduced (0, 7));
        vol.setBounds (row);
    }

private:
    void setup (TextButton& b, Colour on, std::function<void()> f)
    {
        b.setClickingTogglesState (true);
        b.setColour (TextButton::buttonOnColourId, on);
        b.onClick = std::move (f);
        addAndMakeVisible (b);
    }

    Track& t;
    int index;
    std::function<void (int)> onSelect;
    bool selected = false;
    Label nameLabel;
    TextButton mBtn { "M" }, sBtn { "S" }, rBtn { "R" }, iBtn { "I" };
    Slider vol;
    LevelMeter meter;
};

//==============================================================================
class LaneArea : public Component
{
public:
    explicit LaneArea (ArrangeState& s) : st (s) { setWantsKeyboardFocus (true); }

    void paint (Graphics& g) override
    {
        auto& e = st.eng;
        const int H = ArrangeState::trackH, W = getWidth(), n = e.numTracks();
        const double sr = e.sr();
        g.fillAll (Theme::lane);
        for (int i = 0; i < n; ++i)
        {
            g.setColour (i == st.selTrack ? Theme::lane.brighter (0.07f) : (i % 2 ? Theme::laneAlt : Theme::lane));
            g.fillRect (0, i * H, W, H);
        }

        // rejilla (compases / pulsos)
        const double beatSec = 60.0 / e.bpm.load();
        const double barPx = beatSec * 4.0 * st.pxPerSec;
        int stride = 1;
        while (barPx * stride < 28.0) stride *= 2;
        for (auto i = (int64) std::floor (st.scrollSec / beatSec); ; ++i)
        {
            const float x = st.secToX ((double) i * beatSec);
            if (x > (float) W) break;
            const bool bar = (i % 4 == 0);
            if (bar && ((i / 4) % stride) != 0) continue;
            if (! bar && beatSec * st.pxPerSec < 16.0) continue;
            g.setColour (Colours::white.withAlpha (bar ? 0.07f : 0.03f));
            g.drawVerticalLine ((int) x, 0.f, (float) getHeight());
        }

        // clips
        for (int i = 0; i < n; ++i)
        {
            auto& t = e.track (i);
            for (auto& c : t.clips)
            {
                const float x0 = st.secToX ((double) c->start / sr), x1 = st.secToX ((double) (c->start + c->length()) / sr);
                if (x1 < 0.f || x0 > (float) W) continue;
                drawClip (g, *c, Rectangle<float> (x0, (float) (i * H + 3), x1 - x0, (float) (H - 6)), c == st.selClip);
            }
            if (e.isRecording() && t.armed.load())
            {
                const float x0 = st.secToX ((double) t.recStart / sr), x1 = st.secToX ((double) e.playhead.load() / sr);
                Rectangle<float> r (x0, (float) (i * H + 3), jmax (2.f, x1 - x0), (float) (H - 6));
                g.setColour (Theme::recClip);
                g.fillRoundedRectangle (r, 3.f);
                g.setColour (Colours::white.withAlpha (0.8f));
                g.setFont (11.f);
                g.drawText ("Grabando…", r.reduced (4, 0), Justification::centredLeft, true);
            }
        }

        // cabezal
        const float px = st.secToX ((double) e.playhead.load() / sr);
        g.setColour (Theme::red);
        g.drawVerticalLine ((int) px, 0.f, (float) getHeight());
    }

    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        auto& eng = st.eng;
        const int ti = e.y / ArrangeState::trackH;
        st.selClip = nullptr; dragging = false;
        if (e.y < 0 || ti >= eng.numTracks()) { repaint(); return; }
        st.select (ti);
        const auto smp = st.xToSample ((float) e.x);
        auto& tr = eng.track (ti);
        for (int k = (int) tr.clips.size(); --k >= 0;)
        {
            auto& c = tr.clips[(size_t) k];
            if (smp >= c->start && smp < c->start + c->length())
            {
                st.selClip = c; dragging = true; dragOffset = smp - c->start; dragTrack = ti;
                break;
            }
        }
        if (! dragging) eng.setPlayhead (smp);
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (! dragging || st.selClip == nullptr) return;
        auto& eng = st.eng;
        const int target = jlimit (0, eng.numTracks() - 1, e.y / ArrangeState::trackH);
        const auto smp = st.xToSample ((float) e.x);
        ScopedLock sl (eng.processLock);
        st.selClip->start = jmax<int64> (0, smp - dragOffset);
        if (target != dragTrack)
        {
            auto& from = eng.track (dragTrack).clips;
            auto it = std::find (from.begin(), from.end(), st.selClip);
            if (it != from.end())
            {
                from.erase (it);
                eng.track (target).clips.push_back (st.selClip);
                dragTrack = target;
                st.select (target);
            }
        }
        repaint();
    }
    void mouseUp (const MouseEvent&) override { dragging = false; }

    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double anchor = st.xToSec ((float) e.x);
            st.pxPerSec = jlimit (2.0, 800.0, st.pxPerSec * std::exp ((double) w.deltaY * 1.5));
            st.scrollSec = jmax (0.0, anchor - (double) e.x / st.pxPerSec);
        }
        else if (std::abs (w.deltaX) > std::abs (w.deltaY) || e.mods.isShiftDown())
        {
            const double d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            st.scrollSec = jmax (0.0, st.scrollSec - d * 400.0 / st.pxPerSec);
        }
        else
            Component::mouseWheelMove (e, w);
        repaint();
    }

private:
    void drawClip (Graphics& g, const Clip& c, Rectangle<float> r, bool selected)
    {
        g.setColour (selected ? Theme::clipBlue.brighter (0.25f) : Theme::clipBlue);
        g.fillRoundedRectangle (r, 3.f);
        g.setColour (selected ? Theme::teal : Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (r, 3.f, 1.f);
        g.setColour (Colours::white.withAlpha (0.9f));
        g.setFont (11.f);
        g.drawText (c.name, r.reduced (4.f, 0.f).withHeight (14.f), Justification::centredLeft, true);

        auto wave = r.withTrimmedTop (15.f).reduced (0.f, 2.f);
        const float mid = wave.getCentreY(), half = wave.getHeight() * 0.5f;
        const double spp = st.eng.sr() / st.pxPerSec;
        const int pc = (int) (c.peaks.size() / 2);
        g.setColour (Theme::clipWave);
        const int xs = jmax (0, (int) r.getX()), xe = jmin (getWidth(), (int) r.getRight());
        for (int x = xs; x < xe; ++x)
        {
            const double s0 = jmax (0.0, ((double) x - (double) r.getX()) * spp);
            int p0 = (int) (s0 / Clip::peakRes);
            int p1 = jmax (p0, (int) ((s0 + spp) / Clip::peakRes));
            if (p0 >= pc) break;
            p1 = jmin (p1, pc - 1);
            float mn = 0.f, mx = 0.f;
            for (int p = p0; p <= p1; ++p) { mn = jmin (mn, c.peaks[(size_t) (2 * p)]); mx = jmax (mx, c.peaks[(size_t) (2 * p + 1)]); }
            const float top = mid - jmin (1.f, mx) * half;
            const float bot = jmax (top + 1.f, mid - jmax (-1.f, mn) * half);
            g.drawVerticalLine (x, top, bot);
        }
    }

    ArrangeState& st;
    bool dragging = false;
    int64 dragOffset = 0;
    int dragTrack = 0;
};

//==============================================================================
class Ruler : public Component
{
public:
    explicit Ruler (ArrangeState& s) : st (s) { setWantsKeyboardFocus (true); }

    void paint (Graphics& g) override
    {
        auto& e = st.eng;
        const int W = getWidth(), H = getHeight();
        const double beatSec = 60.0 / e.bpm.load(), barSec = beatSec * 4.0;
        g.fillAll (Theme::panel);

        // banda turquesa = duración del proyecto (como en la referencia)
        const double endSec = jmax (barSec * 8.0, (double) e.projectEnd() / e.sr() + barSec);
        const float bandEnd = jlimit (0.f, (float) W, st.secToX (endSec));
        g.setColour (Theme::teal.withAlpha (0.85f));
        g.fillRect (0.f, 4.f, bandEnd, (float) H - 8.f);

        int stride = 1;
        while (barSec * st.pxPerSec * stride < 48.0) stride *= 2;
        g.setFont (11.f);
        for (auto b = (int64) std::floor (st.scrollSec / barSec); ; ++b)
        {
            const float x = st.secToX ((double) b * barSec);
            if (x > (float) W) break;
            if (b % stride != 0) continue;
            g.setColour (x < bandEnd ? Colours::black.withAlpha (0.7f) : Theme::dim);
            g.drawVerticalLine ((int) x, (float) H - 8.f, (float) H - 4.f);
            g.drawText (String (b + 1), (int) x + 3, 4, 40, H - 8, Justification::centredLeft);
        }
        const float px = st.secToX ((double) e.playhead.load() / e.sr());
        g.setColour (Theme::red);
        Path p; p.addTriangle (px - 6.f, 2.f, px + 6.f, 2.f, px, 12.f);
        g.fillPath (p);
        g.drawVerticalLine ((int) px, 10.f, (float) H);
    }
    void mouseDown (const MouseEvent& e) override { grabKeyboardFocus(); st.eng.setPlayhead (st.xToSample ((float) e.x)); }
    void mouseDrag (const MouseEvent& e) override { st.eng.setPlayhead (st.xToSample ((float) e.x)); }

private:
    ArrangeState& st;
};

//==============================================================================
class TracksContent : public Component
{
public:
    static constexpr int headerW = 260;
    explicit TracksContent (ArrangeState& s) : st (s), lanes (s) { addAndMakeVisible (lanes); }

    void rebuild()
    {
        headers.clear();
        for (int i = 0; i < st.eng.numTracks(); ++i)
        {
            auto* h = new TrackHeader (st.eng.track (i), i, [this] (int idx) { st.select (idx); });
            headers.add (h);
            addAndMakeVisible (h);
        }
        resized();
    }
    void resized() override
    {
        const int H = ArrangeState::trackH;
        for (int i = 0; i < headers.size(); ++i) headers[i]->setBounds (0, i * H, headerW, H);
        lanes.setBounds (headerW, 0, getWidth() - headerW, getHeight());
    }
    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    ArrangeState& st;
    OwnedArray<TrackHeader> headers;
    LaneArea lanes;
};

//==============================================================================
class ArrangeView : public Component, private Timer
{
public:
    explicit ArrangeView (Engine& e) : st (e), ruler (st), content (st)
    {
        vp.setViewedComponent (&content, false);
        vp.setScrollBarsShown (true, false);
        addAndMakeVisible (vp);
        addAndMakeVisible (ruler);

        addBtn.onClick = [this] { if (onAddTrack) onAddTrack(); };
        addAndMakeVisible (addBtn);

        zoom.setSliderStyle (Slider::LinearHorizontal);
        zoom.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        zoom.setRange (3.0, 300.0);
        zoom.setSkewFactorFromMidPoint (40.0);
        zoom.setValue (st.pxPerSec, dontSendNotification);
        zoom.onValueChange = [this] { st.pxPerSec = zoom.getValue(); };
        addAndMakeVisible (zoom);
        startTimerHz (30);
    }
    ~ArrangeView() override { vp.setViewedComponent (nullptr, false); }

    void rebuild() { content.rebuild(); refreshLayout(); }
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (28);
        auto left = top.removeFromLeft (TracksContent::headerW);
        addBtn.setBounds (left.removeFromLeft (84).reduced (3));
        zoom.setBounds (left.reduced (4, 4));
        ruler.setBounds (top);
        vp.setBounds (r);
        refreshLayout();
    }
    void paint (Graphics& g) override { g.fillAll (Theme::panel); }

    std::function<void()> onAddTrack;
    ArrangeState st;

private:
    void refreshLayout()
    {
        content.setSize (vp.getMaximumVisibleWidth(),
                         jmax (vp.getMaximumVisibleHeight(), st.eng.numTracks() * ArrangeState::trackH + 40));
    }
    void timerCallback() override
    {
        auto& e = st.eng;
        if (e.isPlaying())
        {
            const double t = (double) e.playhead.load() / e.sr();
            const float x = st.secToX (t);
            if (x > (float) ruler.getWidth() - 40.f || x < 0.f) st.scrollSec = jmax (0.0, t - 1.0);
        }
        for (int i = 0; i < content.headers.size(); ++i) content.headers[i]->tick (i == st.selTrack);
        ruler.repaint();
        content.lanes.repaint();
    }

    Ruler ruler;
    Viewport vp;
    TracksContent content;
    TextButton addBtn { "+ Pista" };
    Slider zoom;
};

//==============================================================================
struct Knob : public Component
{
    Slider s { Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow };
    String title;
    Knob (const String& t, double mn, double mx, double def, const String& suffix, double midpoint = 0.0) : title (t)
    {
        s.setRange (mn, mx);
        if (midpoint > 0.0) s.setSkewFactorFromMidPoint (midpoint);
        s.setDoubleClickReturnValue (true, def);
        s.setValue (def, dontSendNotification);
        s.setTextValueSuffix (suffix);
        s.setNumDecimalPlacesToDisplay (1);
        s.setTextBoxStyle (Slider::TextBoxBelow, false, 62, 14);
        addAndMakeVisible (s);
    }
    void resized() override { s.setBounds (getLocalBounds().withTrimmedTop (13)); }
    void paint (Graphics& g) override
    {
        g.setColour (Theme::dim);
        g.setFont (10.5f);
        g.drawText (title, getLocalBounds().removeFromTop (13), Justification::centred);
    }
};

//==============================================================================
class ChannelStrip : public Component
{
public:
    ChannelStrip (Engine& e, AudioDeviceManager& dm, std::function<void()> managePlugins)
        : eng (e), devices (dm), onManage (std::move (managePlugins)),
          eqLow ("GRAVES", -15, 15, 0, " dB"), eqMid ("MEDIOS", -15, 15, 0, " dB"),
          eqHigh ("AGUDOS", -15, 15, 0, " dB"), eqFreq ("FREC.", 200, 5000, 1000, " Hz", 1000),
          cThr ("UMBRAL", -60, 0, -18, " dB"), cRatio ("RATIO", 1, 20, 3, ":1"),
          cAtk ("ATAQUE", 0.1, 200, 10, " ms", 20), cRel ("RELAJ.", 5, 1000, 120, " ms", 150),
          cMk ("GANANCIA", 0, 24, 0, " dB"), pan ("PAN", -100, 100, 0, " %")
    {
        for (auto* c : std::initializer_list<Component*> { &inputBox, &eqOn, &compOn, &eqLow, &eqMid, &eqHigh, &eqFreq,
                                                          &cThr, &cRatio, &cAtk, &cRel, &cMk, &pan })
            addAndMakeVisible (c);

        for (auto* b : { &eqOn, &compOn })
        {
            b->setClickingTogglesState (true);
            b->setColour (TextButton::buttonOnColourId, Theme::teal);
        }
        eqOn.onClick   = [this] { if (cur) cur->eq.enabled = eqOn.getToggleState(); };
        compOn.onClick = [this] { if (cur) cur->comp.enabled = compOn.getToggleState(); };

        inputBox.onChange = [this] { if (cur) cur->inputChannel = inputBox.getSelectedId() - 1; };

        eqLow.s.onValueChange  = [this] { if (cur) cur->eq.lowDb  = (float) eqLow.s.getValue(); };
        eqMid.s.onValueChange  = [this] { if (cur) cur->eq.midDb  = (float) eqMid.s.getValue(); };
        eqHigh.s.onValueChange = [this] { if (cur) cur->eq.highDb = (float) eqHigh.s.getValue(); };
        eqFreq.s.onValueChange = [this] { if (cur) cur->eq.midFreq = (float) eqFreq.s.getValue(); };
        cThr.s.onValueChange   = [this] { if (cur) cur->comp.thresholdDb = (float) cThr.s.getValue(); };
        cRatio.s.onValueChange = [this] { if (cur) cur->comp.ratio = (float) cRatio.s.getValue(); };
        cAtk.s.onValueChange   = [this] { if (cur) cur->comp.attackMs = (float) cAtk.s.getValue(); };
        cRel.s.onValueChange   = [this] { if (cur) cur->comp.releaseMs = (float) cRel.s.getValue(); };
        cMk.s.onValueChange    = [this] { if (cur) cur->comp.makeupDb = (float) cMk.s.getValue(); };
        pan.s.onValueChange    = [this] { if (cur) cur->pan = (float) pan.s.getValue() / 100.f; };

        for (int i = 0; i < kMaxInserts; ++i)
        {
            auto* b = slotBtns.add (new TextButton());
            b->onClick = [this, i] { slotClicked (i); };
            addAndMakeVisible (b);
        }
    }

    void setTrack (Track* t)
    {
        cur = nullptr;
        if (t != nullptr)
        {
            eqOn.setToggleState (t->eq.enabled.load(), dontSendNotification);
            compOn.setToggleState (t->comp.enabled.load(), dontSendNotification);
            eqLow.s.setValue (t->eq.lowDb.load(), dontSendNotification);
            eqMid.s.setValue (t->eq.midDb.load(), dontSendNotification);
            eqHigh.s.setValue (t->eq.highDb.load(), dontSendNotification);
            eqFreq.s.setValue (t->eq.midFreq.load(), dontSendNotification);
            cThr.s.setValue (t->comp.thresholdDb.load(), dontSendNotification);
            cRatio.s.setValue (t->comp.ratio.load(), dontSendNotification);
            cAtk.s.setValue (t->comp.attackMs.load(), dontSendNotification);
            cRel.s.setValue (t->comp.releaseMs.load(), dontSendNotification);
            cMk.s.setValue (t->comp.makeupDb.load(), dontSendNotification);
            pan.s.setValue (t->pan.load() * 100.0, dontSendNotification);
        }
        cur = t;
        refreshInputs();
        refreshSlots();
        repaint();
    }

    void refreshInputs()
    {
        inputBox.clear (dontSendNotification);
        if (auto* d = devices.getCurrentAudioDevice())
        {
            auto names = d->getInputChannelNames();
            for (int i = 0; i < names.size(); ++i) inputBox.addItem ("Entrada " + String (i + 1) + " – " + names[i], i + 1);
            if (cur != nullptr) inputBox.setSelectedId (jlimit (0, jmax (0, names.size() - 1), cur->inputChannel.load()) + 1, dontSendNotification);
        }
        if (inputBox.getNumItems() == 0) inputBox.setTextWhenNothingSelected ("Sin entradas de audio");
    }

    void refreshSlots()
    {
        for (int i = 0; i < kMaxInserts; ++i)
        {
            auto* p = cur ? cur->plugins[(size_t) i].get() : nullptr;
            const bool byp = cur && cur->bypass[(size_t) i].load();
            slotBtns[i]->setButtonText (p ? (byp ? "(bypass) " : "") + p->getName() : String (i + 1) + "   — vacío —");
            slotBtns[i]->setColour (TextButton::buttonColourId, p ? (byp ? Theme::panel2 : Theme::teal.darker (0.55f)) : Theme::panel);
            slotBtns[i]->setColour (TextButton::textColourOffId, p ? Colours::white : Theme::dim);
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::teal);
        g.setFont (Font (13.f, Font::bold));
        g.drawText (cur ? "PISTA: " + cur->name : String ("SIN PISTA"), 10, 4, getWidth() - 20, 20, Justification::centredLeft);
        g.setColour (Theme::dim);
        g.setFont (Font (11.f, Font::bold));
        g.drawText ("ECUALIZADOR", 10, secEq, 120, 14, Justification::centredLeft);
        g.drawText ("COMPRESOR", 10, secComp, 120, 14, Justification::centredLeft);
        g.drawText ("INSERTS (VST3 x10)", 10, secIns, 160, 14, Justification::centredLeft);
        g.drawText ("PANORAMA", 10, secPan, 100, 14, Justification::centredLeft);
    }

    void resized() override
    {
        const int w = getWidth();
        inputBox.setBounds (8, 28, w - 16, 24);
        eqOn.setBounds (w - 48, secEq - 2, 40, 18);
        compOn.setBounds (w - 48, secComp - 2, 40, 18);

        const int kw = (w - 16) / 4;
        Knob* eq[] = { &eqLow, &eqMid, &eqHigh, &eqFreq };
        for (int i = 0; i < 4; ++i) eq[i]->setBounds (8 + i * kw, secEq + 16, kw, 70);

        const int cw = (w - 16) / 3;
        Knob* c1[] = { &cThr, &cRatio, &cAtk };
        for (int i = 0; i < 3; ++i) c1[i]->setBounds (8 + i * cw, secComp + 16, cw, 70);
        cRel.setBounds (8, secComp + 88, cw, 70);
        cMk.setBounds (8 + cw, secComp + 88, cw, 70);

        for (int i = 0; i < kMaxInserts; ++i) slotBtns[i]->setBounds (8, secIns + 18 + i * 21, w - 16, 19);
        pan.setBounds (8, secPan + 16, cw, 70);
    }

private:
    void slotClicked (int i)
    {
        if (cur == nullptr) return;
        auto* t = cur;
        auto* p = t->plugins[(size_t) i].get();
        auto types = eng.knownPlugins.getTypes();

        PopupMenu m;
        if (p != nullptr)
        {
            m.addItem (1, "Abrir editor");
            m.addItem (2, t->bypass[(size_t) i].load() ? "Activar" : "Bypass");
            m.addItem (3, "Quitar");
            m.addSeparator();
        }
        PopupMenu list;
        int added = 0;
        for (int k = 0; k < types.size(); ++k)
            if (! types[k].isInstrument) { list.addItem (100 + k, types[k].name + "   (" + types[k].pluginFormatName + ")"); ++added; }
        if (added == 0) list.addItem (9999, "(sin plugins – usa Administrar plugins)", false);
        m.addSubMenu (p ? "Reemplazar con…" : "Cargar plugin…", list);
        m.addSeparator();
        m.addItem (4, "Administrar plugins…");

        m.showMenuAsync (PopupMenu::Options().withTargetComponent (slotBtns[i]),
            [this, i, t, types] (int r)
            {
                if (r == 0 || t != cur) return;
                auto* pl = t->plugins[(size_t) i].get();
                if (r == 1 && pl) PluginWindows::open (*pl);
                else if (r == 2) { t->bypass[(size_t) i] = ! t->bypass[(size_t) i].load(); }
                else if (r == 3 && pl) { PluginWindows::close (pl); eng.clearPlugin (*t, i); }
                else if (r == 4) { if (onManage) onManage(); }
                else if (r >= 100 && r < 100 + types.size())
                {
                    if (pl) PluginWindows::close (pl);
                    String err;
                    if (! eng.loadPlugin (*t, i, types[r - 100], err))
                        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "No se pudo cargar el plugin", err);
                }
                refreshSlots();
            });
    }

    static constexpr int secEq = 62, secComp = 156, secIns = 322, secPan = 552;

    Engine& eng;
    AudioDeviceManager& devices;
    std::function<void()> onManage;
    Track* cur = nullptr;

    ComboBox inputBox;
    TextButton eqOn { "ON" }, compOn { "ON" };
    Knob eqLow, eqMid, eqHigh, eqFreq, cThr, cRatio, cAtk, cRel, cMk, pan;
    OwnedArray<TextButton> slotBtns;
};

//==============================================================================
class MasterStrip : public Component
{
public:
    explicit MasterStrip (Engine& e) : eng (e)
    {
        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        fader.setRange (-60.0, 6.0);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setValue (0.0, dontSendNotification);
        fader.onValueChange = [this]
        {
            eng.masterDb = (float) fader.getValue();
            val.setText (String (fader.getValue(), 1) + " dB", dontSendNotification);
        };
        addAndMakeVisible (fader);
        meter.vertical = true;
        meter.source = [this] { return eng.masterLevel.load(); };
        addAndMakeVisible (meter);
        val.setJustificationType (Justification::centred);
        val.setText ("0.0 dB", dontSendNotification);
        val.setFont (Font (11.f));
        addAndMakeVisible (val);
    }
    void tick() { meter.tick(); }
    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel2);
        g.setColour (Theme::teal);
        g.setFont (Font (13.f, Font::bold));
        g.drawText ("MASTER", 0, 4, getWidth(), 20, Justification::centred);
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedTop (30);
        val.setBounds (r.removeFromBottom (20));
        r.reduce (6, 6);
        fader.setBounds (r.removeFromLeft (32));
        r.removeFromLeft (4);
        meter.setBounds (r.removeFromLeft (14));
    }
private:
    Engine& eng;
    Slider fader;
    LevelMeter meter;
    Label val;
};

//==============================================================================
class TransportBar : public Component, private Timer
{
public:
    TransportBar (Engine& e, AudioDeviceManager& dm, ApplicationCommandManager& cm) : eng (e), devices (dm)
    {
        auto wire = [this, &cm] (IconButton& b, int cmd) { b.onClick = [&cm, cmd] { cm.invokeDirectly (cmd, true); }; addAndMakeVisible (b); };
        wire (rewind, Cmd::rewind);
        wire (stop, Cmd::play);       // detiene o inicia
        wire (play, Cmd::play);
        wire (rec, Cmd::record);
        wire (metro, Cmd::metronome);
        stop.onClick = [this, &cm] { eng.stop(); (void) cm; };

        bpm.setSliderStyle (Slider::IncDecButtons);
        bpm.setRange (40.0, 240.0, 1.0);
        bpm.setValue (120.0, dontSendNotification);
        bpm.setTextBoxStyle (Slider::TextBoxLeft, false, 52, 26);
        bpm.setIncDecButtonsMode (Slider::incDecButtonsDraggable_Vertical);
        bpm.onValueChange = [this] { eng.bpm = bpm.getValue(); };
        addAndMakeVisible (bpm);

        info.setJustificationType (Justification::centredRight);
        info.setColour (Label::textColourId, Theme::dim);
        info.setFont (Font (12.f));
        addAndMakeVisible (info);
        startTimerHz (30);
    }

    void setBpm (double v) { bpm.setValue (v, dontSendNotification); }

    void paint (Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::teal);
        g.setFont (Font (20.f, Font::bold));
        g.drawText ("EC AUDIO", 16, 0, 140, getHeight(), Justification::centredLeft);

        auto box = Rectangle<int> (displayX, 8, 310, getHeight() - 16);
        g.setColour (Colour (0xff0a1218));
        g.fillRoundedRectangle (box.toFloat(), 8.f);

        const double sec = (double) eng.playhead.load() / eng.sr();
        const double beats = sec * eng.bpm.load() / 60.0;
        const int bar = (int) (beats / 4.0) + 1, beat = (int) std::fmod (beats, 4.0) + 1;
        g.setColour (Theme::teal);
        g.setFont (Font (30.f, Font::bold));
        g.drawText (String (bar).paddedLeft ('0', 3) + "  " + String (beat), box.removeFromLeft (150), Justification::centred);
        g.setColour (Theme::text);
        g.setFont (Font (19.f));
        g.drawText (String::formatted ("%02d:%06.3f", (int) (sec / 60.0), std::fmod (sec, 60.0)), box, Justification::centred);
        g.setColour (Theme::dim);
        g.setFont (Font (10.f));
        g.drawText ("TEMPO", bpm.getX(), 4, 60, 12, Justification::centredLeft);
    }

    void resized() override
    {
        int x = 170;
        for (auto* b : { &rewind, &stop, &play, &rec, &metro }) { b->setBounds (x, 10, 44, getHeight() - 20); x += 50; }
        displayX = x + 14;
        bpm.setBounds (displayX + 310 + 24, 16, 130, getHeight() - 32);
        info.setBounds (getWidth() - 360, 0, 346, getHeight());
    }

private:
    void timerCallback() override
    {
        play.lit = eng.isPlaying();
        rec.lit = eng.isRecording();
        metro.lit = eng.metronome.load();
        play.repaint(); rec.repaint(); metro.repaint();
        repaint (displayX, 0, 320, getHeight());

        if (++counter % 15 == 0)
            if (auto* d = devices.getCurrentAudioDevice())
            {
                const double sr = d->getCurrentSampleRate();
                const double ms = (d->getInputLatencyInSamples() + d->getOutputLatencyInSamples()) / sr * 1000.0;
                info.setText (d->getName() + "  ·  " + String (sr, 0) + " Hz  ·  " + String (d->getCurrentBufferSizeSamples())
                              + " smp  ·  " + String (ms, 1) + " ms", dontSendNotification);
            }
    }

    Engine& eng;
    AudioDeviceManager& devices;
    IconButton rewind { IconButton::Rewind }, stop { IconButton::Stop }, play { IconButton::Play },
               rec { IconButton::Rec }, metro { IconButton::Metro };
    Slider bpm;
    Label info;
    int displayX = 440, counter = 0;
};
