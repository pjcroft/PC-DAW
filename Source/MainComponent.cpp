#include "MainComponent.h"

//==============================================================================
//  IconButton
//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i, juce::Colour led)
    : Button (name), icon (i)
{
    setColour (juce::TextButton::buttonOnColourId, led);
    setTooltip (name);
    setWantsKeyboardFocus (false);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, over, down);

    auto r = getLocalBounds().toFloat().reduced (getHeight() * 0.28f);
    auto s = juce::jmin (r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre (s, s);

    const bool on = getToggleState();
    auto ink = on ? juce::Colours::white : Theme::textOnChrome;

    if (icon == Icon::record && ! on)
        ink = Theme::record.darker (0.2f);

    g.setColour (ink);
    juce::Path p;

    switch (icon)
    {
        case Icon::play:
            p.addTriangle (r.getX() + s * 0.15f, r.getY(), r.getX() + s * 0.15f, r.getBottom(), r.getRight(), r.getCentreY());
            g.fillPath (p);
            break;

        case Icon::stop:
            g.fillRect (r.reduced (s * 0.08f));
            break;

        case Icon::record:
            g.fillEllipse (r.reduced (s * 0.04f));
            break;

        case Icon::rewind:
            g.fillRect (r.getX(), r.getY(), s * 0.14f, s);
            p.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(), r.getX() + s * 0.18f, r.getCentreY());
            g.fillPath (p);
            break;

        case Icon::loop:
        {
            auto a = r.reduced (s * 0.05f, s * 0.18f);
            p.addRoundedRectangle (a, a.getHeight() * 0.45f);
            g.strokePath (p, juce::PathStrokeType (s * 0.12f));
            juce::Path arrow;
            arrow.addTriangle (a.getCentreX() - s * 0.05f, a.getY() - s * 0.16f,
                               a.getCentreX() - s * 0.05f, a.getY() + s * 0.16f,
                               a.getCentreX() + s * 0.2f, a.getY());
            g.fillPath (arrow);
            break;
        }

        case Icon::click:
        case Icon::metronome:
        {
            p.addTriangle (r.getCentreX(), r.getY(), r.getX() + s * 0.1f, r.getBottom(), r.getRight() - s * 0.1f, r.getBottom());
            g.strokePath (p, juce::PathStrokeType (s * 0.1f, juce::PathStrokeType::curved));
            g.drawLine (r.getCentreX(), r.getBottom() - s * 0.2f, r.getRight() - s * 0.05f, r.getY() + s * 0.1f, s * 0.1f);
            break;
        }
    }
}

//==============================================================================
//  TransportBar
//==============================================================================
TransportBar::TransportBar (Session& s) : session (&s)
{
    for (auto b : { &rewindButton, &stopButton, &playButton, &recordButton, &loopButton, &clickButton })
        addAndMakeVisible (b);

    rewindButton.onClick = [this] { session->edit->getTransport().setPosition (te::TimePosition()); };
    playButton.onClick = [this]
    {
        auto& t = session->edit->getTransport();
        if (t.isPlaying()) t.stop (false, false);
        else               t.play (false);
    };
    stopButton.onClick = [this]
    {
        auto& t = session->edit->getTransport();
        if (t.isPlaying() || t.isRecording())
        {
            const bool wasRecording = t.isRecording();
            t.stop (false, false);
            if (wasRecording) session->save();
        }
        else
        {
            t.setPosition (te::TimePosition());
        }
    };
    recordButton.onClick = [this] { if (onRecord) onRecord(); };
    loopButton.onClick = [this]
    {
        auto& t = session->edit->getTransport();
        if (t.getLoopRange().isEmpty())
        {
            auto& tempo = session->edit->tempoSequence;
            t.setLoopRange ({ te::TimePosition(), tempo.toTime (te::BeatPosition::fromBeats (16.0)) });
        }
        t.looping = ! t.looping.get();
    };
    clickButton.onClick = [this] { session->edit->clickTrackEnabled = ! session->edit->clickTrackEnabled.get(); };

    bpmSlider.setRange (20.0, 300.0, 1.0);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 50, 24);
    bpmSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    bpmSlider.setTooltip ("Tempo (drag the arrows, or type a value)");
    bpmSlider.onValueChange = [this] { session->setBpm (bpmSlider.getValue()); };
    addAndMakeVisible (bpmSlider);

    countInBox.addItem ("No Count-in", 1);
    countInBox.addItem ("1 Bar Count-in", 2);
    countInBox.addItem ("2 Bar Count-in", 3);
    countInBox.setTooltip ("Count-in before recording");
    countInBox.setWantsKeyboardFocus (false);
    countInBox.onChange = [this]
    {
        using C = te::Edit::CountIn;
        auto id = countInBox.getSelectedId();
        session->edit->setCountInMode (id == 2 ? C::oneBar : id == 3 ? C::twoBar : C::none);
    };
    addAndMakeVisible (countInBox);

    sessionName.setFont (juce::FontOptions (12.0f));
    sessionName.setColour (juce::Label::textColourId, Theme::textDim);
    sessionName.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (sessionName);

    bind();
    startTimerHz (30);
}

void TransportBar::setSession (Session& s)
{
    session = &s;
    bind();
}

void TransportBar::bind()
{
    bpmSlider.setValue (session->getBpm(), juce::dontSendNotification);

    using C = te::Edit::CountIn;
    auto mode = session->edit->getCountInMode();
    countInBox.setSelectedId (mode == C::oneBar ? 2 : mode == C::twoBar ? 3 : 1, juce::dontSendNotification);
    sessionName.setText (session->getName(), juce::dontSendNotification);
    timerCallback();
}

void TransportBar::timerCallback()
{
    auto& edit = *session->edit;
    auto& t = edit.getTransport();

    playButton.setToggleState (t.isPlaying() && ! t.isRecording(), juce::dontSendNotification);
    recordButton.setToggleState (t.isRecording(), juce::dontSendNotification);
    loopButton.setToggleState (t.looping.get(), juce::dontSendNotification);
    clickButton.setToggleState (edit.clickTrackEnabled.get(), juce::dontSendNotification);

    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (session->getBpm(), juce::dontSendNotification);

    auto pos = t.getPosition();
    auto bb = edit.tempoSequence.toBarsAndBeats (pos);
    auto beat = bb.getWholeBeats() + 1;
    auto ticks = juce::roundToInt (bb.getFractionalBeats().inBeats() * 960.0) % 960;

    auto newBars = juce::String (bb.bars + 1).paddedLeft (' ', 3) + " . " + juce::String (beat) + " . " + juce::String (ticks).paddedLeft ('0', 3);

    auto secs = juce::jmax (0.0, pos.inSeconds());
    auto mins = (int) (secs / 60.0);
    auto newTime = juce::String (mins) + ":" + juce::String ((int) secs % 60).paddedLeft ('0', 2) + "."
                    + juce::String ((int) (std::fmod (secs, 1.0) * 1000.0)).paddedLeft ('0', 3);

    auto newCpu = edit.engine.getDeviceManager().getCpuUsage();

    if (newBars != barsText || newTime != timeText)
    {
        barsText = newBars;
        timeText = newTime;
        repaint (displayArea);
    }

    if (std::abs (newCpu - cpu) > 0.01f)
    {
        cpu = newCpu;
        repaint (cpuArea);
    }
}

void TransportBar::paint (juce::Graphics& g)
{
    Theme::fillPanel (g, getLocalBounds().toFloat());

    // Logo
    Logo::drawBadge (g, juce::Rectangle<float> (10.0f, 6.0f, 52.0f, 52.0f));
    Logo::drawWordmark (g, juce::Rectangle<float> (70.0f, 9.0f, 120.0f, 30.0f));

    // LCD-style counter
    auto lcd = displayArea.toFloat();
    g.setColour (juce::Colour (0xff04060b));
    g.fillRoundedRectangle (lcd, 5.0f);
    g.setGradientFill (juce::ColourGradient (Theme::chromeLo, 0, lcd.getY(), Theme::chromeEdge, 0, lcd.getBottom(), false));
    g.drawRoundedRectangle (lcd, 5.0f, 1.5f);

    auto inner = lcd.reduced (12.0f, 4.0f);
    g.setColour (Theme::accent);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold));
    g.drawText (barsText, inner.removeFromLeft (inner.getWidth() * 0.55f), juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
    g.drawText (timeText, inner, juce::Justification::centredRight);

    // CPU
    auto c = cpuArea.toFloat();
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("CPU", c.removeFromTop (12.0f), juce::Justification::centredLeft);
    auto bar = c.removeFromTop (6.0f);
    g.setColour (Theme::bgDeep);
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (cpu > 0.8f ? Theme::record : cpu > 0.5f ? Theme::mute : Theme::accent);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, cpu)), 2.0f);
    g.setColour (Theme::textDim);
    g.drawText (juce::String (juce::roundToInt (cpu * 100.0f)) + "%", c, juce::Justification::centredLeft);

    // BPM label
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("BPM", bpmSlider.getBounds().translated (0, -14).withHeight (12), juce::Justification::centredLeft);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromLeft (196);
    sessionName.setBounds (70, getHeight() - 20, 126, 16);

    auto row = r.withSizeKeepingCentre (r.getWidth(), 36);
    for (auto b : { &rewindButton, &stopButton, &playButton, &recordButton })
    {
        b->setBounds (row.removeFromLeft (46).reduced (2, 0));
    }
    row.removeFromLeft (10);
    loopButton.setBounds (row.removeFromLeft (40).reduced (2, 2));
    clickButton.setBounds (row.removeFromLeft (40).reduced (2, 2));
    row.removeFromLeft (16);

    displayArea = row.removeFromLeft (320).withSizeKeepingCentre (320, 44);
    row.removeFromLeft (16);

    auto bpmArea = row.removeFromLeft (100);
    bpmSlider.setBounds (bpmArea.withSizeKeepingCentre (100, 26).translated (0, 6));
    row.removeFromLeft (12);
    countInBox.setBounds (row.removeFromLeft (130).withSizeKeepingCentre (130, 24).translated (0, 6));

    cpuArea = r.removeFromRight (70).withSizeKeepingCentre (70, 34);
}

//==============================================================================
//  ArrangeView
//==============================================================================
ArrangeView::ArrangeView (Session& s, TimelineState& st)
    : session (s), state (st), ruler (s, st)
{
    addAndMakeVisible (ruler);

    viewport.setViewedComponent (&trackList, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    hScroll.addListener (this);
    hScroll.setAutoHide (false);
    addAndMakeVisible (hScroll);

    addAndMakeVisible (playhead);

    state.addChangeListener (this);
    session.edit->state.addListener (this);

    rebuildTracks();
    startTimerHz (30);
}

ArrangeView::~ArrangeView()
{
    session.edit->state.removeListener (this);
    state.removeChangeListener (this);
}

juce::String ArrangeView::computeRowKey() const
{
    juce::String key;

    for (auto t : session.getTracks())
    {
        key << t->itemID.toString() << ",";

        if (Session::areTakesShown (*t))
            for (auto lane : session.getTakeLanes (*t))
                key << "L" << lane->itemID.toString() << ",";
    }

    return key;
}

void ArrangeView::rebuildTracks()
{
    headers.clear();
    lanes.clear();
    rowHeights.clear();

    for (auto t : session.getTracks())
    {
        trackList.addAndMakeVisible (headers.add (new TrackHeader (session, *t, state)));
        trackList.addAndMakeVisible (lanes.add (new TrackLane (session, *t, state)));
        rowHeights.add (trackHeight);

        if (Session::areTakesShown (*t))
        {
            for (auto lane : session.getTakeLanes (*t))
            {
                trackList.addAndMakeVisible (headers.add (new TakeLaneHeader (session, *lane)));
                trackList.addAndMakeVisible (lanes.add (new TrackLane (session, *lane, state)));
                rowHeights.add (takeLaneHeight);
            }
        }
    }

    rowKey = computeRowKey();
    layoutTracks();
    repaint (0, 0, headerWidth, rulerHeight);
}

void ArrangeView::refreshInputs()
{
    for (auto h : headers)
        if (auto th = dynamic_cast<TrackHeader*> (h))
            th->refreshInputs();
}

void ArrangeView::layoutTracks()
{
    int total = 0;
    for (auto h : rowHeights) total += h;

    auto w = viewport.getMaximumVisibleWidth();
    auto contentH = juce::jmax (viewport.getMaximumVisibleHeight(), total + trackHeight);
    trackList.setSize (juce::jmax (w, headerWidth + 50), contentH);

    int y = 0;

    for (int i = 0; i < headers.size(); ++i)
    {
        auto h = rowHeights[i];
        headers[i]->setBounds (0, y, headerWidth, h);
        lanes[i]->setBounds (headerWidth, y, trackList.getWidth() - headerWidth, h);
        y += h;
    }
}

void ArrangeView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgDeep);

    auto corner = juce::Rectangle<int> (0, 0, headerWidth, rulerHeight);
    Theme::fillPanel (g, corner.toFloat());
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("TRACKS  " + juce::String (session.getRecordTracks().size()), corner.reduced (10, 0), juce::Justification::centredLeft);
    g.drawText (state.snapToBeats ? "SNAP: BEAT" : "SNAP: OFF", corner.reduced (10, 0), juce::Justification::centredRight);
    g.setColour (Theme::chromeEdge);
    g.drawVerticalLine (headerWidth - 1, 0.0f, (float) rulerHeight);
}

void ArrangeView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (rulerHeight);
    ruler.setBounds (top.withTrimmedLeft (headerWidth));

    auto bottom = r.removeFromBottom (12);
    hScroll.setBounds (bottom.withTrimmedLeft (headerWidth));
    viewport.setBounds (r);
    layoutTracks();

    playhead.setBounds (headerWidth, 0, getWidth() - headerWidth - viewport.getScrollBarThickness(), r.getBottom());
    updateScrollBar();
}

void ArrangeView::updateScrollBar()
{
    auto laneWidth = juce::jmax (1, getWidth() - headerWidth);
    auto visibleSecs = laneWidth / state.pixelsPerSecond;
    auto total = juce::jmax (session.edit->getLength().inSeconds() + visibleSecs * 0.5,
                             state.viewStart + visibleSecs, 300.0);
    hScroll.setRangeLimits (0.0, total, juce::dontSendNotification);
    hScroll.setCurrentRange (state.viewStart, visibleSecs, juce::dontSendNotification);
}

void ArrangeView::zoomBy (double factor)
{
    auto anchor = juce::jmax (0.0f, state.timeToX (session.edit->getTransport().getPosition().inSeconds()));
    state.setZoom (state.pixelsPerSecond * factor, anchor);
}

void ArrangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    juce::ignoreUnused (e, w);
}

void ArrangeView::scrollBarMoved (juce::ScrollBar*, double newStart)
{
    state.setViewStart (newStart);
}

void ArrangeView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (computeRowKey() != rowKey)
    {
        rebuildTracks();
        return;
    }

    ruler.repaint();

    for (auto h : headers) h->repaint();
    for (auto l : lanes)   l->repaint();

    updateScrollBar();
    repaint (0, 0, headerWidth, rulerHeight);
}

void ArrangeView::timerCallback()
{
    auto& t = session.edit->getTransport();
    auto pos = t.getPosition().inSeconds();
    auto x = state.timeToX (pos);
    auto laneWidth = (float) playhead.getWidth();

    // Follow the playhead while playing
    if (t.isPlaying() && (x > laneWidth - 20.0f || x < 0.0f))
    {
        state.setViewStart (pos - (laneWidth * 0.1) / state.pixelsPerSecond);
        x = state.timeToX (pos);
    }

    playhead.setX (x);

    for (auto l : lanes)
        l->updateRecording();

    // Loop range can change from the transport bar
    static te::TimeRange lastLoop;
    static bool lastLooping = false;
    auto loop = t.getLoopRange();

    if (loop != lastLoop || t.looping.get() != lastLooping)
    {
        lastLoop = loop;
        lastLooping = t.looping.get();
        ruler.repaint();
    }
}

//==============================================================================
//  MainComponent
//==============================================================================
MainComponent::MainComponent (te::Engine& e, const juce::File& initialSession)
    : engine (e)
{
    juce::PropertiesFile::Options opts;
    opts.applicationName     = "Croft DAW";
    opts.filenameSuffix      = "settings";
    opts.folderName          = "Croft DAW";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties.setStorageParameters (opts);

    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    statusBar.setFont (juce::FontOptions (12.0f));
    statusBar.setColour (juce::Label::textColourId, Theme::textDim);
    statusBar.setColour (juce::Label::backgroundColourId, Theme::bgPanel);
    addAndMakeVisible (statusBar);

    Session::configureInputDevices (engine);
    engine.getDeviceManager().addChangeListener (this);

    auto last = juce::File (appProperties.getUserSettings()->getValue ("lastSession"));
    mixerMode = (MixerPanel::Mode) juce::jlimit (0, 2, appProperties.getUserSettings()->getIntValue ("mixerMode", (int) MixerPanel::Mode::docked));
    dockedMixerHeight = appProperties.getUserSettings()->getIntValue ("mixerHeight", 470);
    openSession (initialSession.existsAsFile() ? initialSession
                                               : last.existsAsFile() ? last : Session::makeNewSessionFile ("Untitled"));

    setStatus ("Ready. Space = play/stop, R = record, Enter = return to start, L = loop, C = click, X = mixer.");
    setSize (1440, 940);
}

MainComponent::~MainComponent()
{
    engine.getDeviceManager().removeChangeListener (this);
    destroyMixer();
    arrange = nullptr;
    transport = nullptr;
    session = nullptr;
    setLookAndFeel (nullptr);
}

void MainComponent::parentHierarchyChanged()
{
    if (auto w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        if (session != nullptr)
            w->setName ("Croft DAW - " + session->getName());
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgDeep);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    if (transport) transport->setBounds (r.removeFromTop (64));
    statusBar.setBounds (r.removeFromBottom (22));

    if (mixerMode == MixerPanel::Mode::docked && mixer != nullptr)
    {
        // Clamp for this layout only, so a tiny window at start-up doesn't overwrite the saved height
        auto h = juce::jlimit (200, juce::jmax (200, r.getHeight() - 120), dockedMixerHeight);
        mixer->setBounds (r.removeFromBottom (h));
        if (dockDivider) dockDivider->setBounds (r.removeFromBottom (6));
    }

    if (arrange) arrange->setBounds (r);
}

//==============================================================================
namespace
{
    /** Drag bar between the tracks and the docked mixer. */
    struct DockDivider : public juce::Component
    {
        std::function<void (int)> onDrag;
        int startHeight = 0;
        std::function<int()> getHeightNow;

        DockDivider() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); }
        void paint (juce::Graphics& g) override
        {
            g.setGradientFill (Theme::chromeGradient (getLocalBounds().toFloat()));
            g.fillAll();
            g.setColour (Theme::chromeEdge);
            for (int i = -1; i <= 1; ++i)
                g.fillRect (getWidth() / 2 + i * 8 - 2, 2, 4, 2);
        }
        void mouseDown (const juce::MouseEvent&) override  { startHeight = getHeightNow(); }
        void mouseDrag (const juce::MouseEvent& e) override { onDrag (startHeight - e.getDistanceFromDragStartY()); }
    };

    /** The pop-out mixer window. Closing it hides the mixer. */
    struct MixerWindow : public juce::DocumentWindow
    {
        std::function<void()> onClose;

        MixerWindow() : DocumentWindow ("Croft DAW - Mixer", Theme::bgDeep, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setResizeLimits (400, 300, 10000, 10000);
        }

        void closeButtonPressed() override { if (onClose) onClose(); }
    };
}

void MainComponent::createMixer()
{
    if (mixer != nullptr || session == nullptr)
        return;

    mixer = std::make_unique<MixerPanel> (*session, timeline);
    mixer->onPopOut = [this] { setMixerMode (MixerPanel::Mode::window); };
    mixer->onDock   = [this] { setMixerMode (MixerPanel::Mode::docked); };
    mixer->onHide   = [this] { setMixerMode (MixerPanel::Mode::hidden); };
}

void MainComponent::destroyMixer()
{
    if (mixerWindow != nullptr)
    {
        appProperties.getUserSettings()->setValue ("mixerWindowState", mixerWindow->getWindowStateAsString());
        mixerWindow->clearContentComponent();
        mixerWindow = nullptr;
    }

    if (mixer != nullptr)
        removeChildComponent (mixer.get());

    mixer = nullptr;
}

void MainComponent::setMixerMode (MixerPanel::Mode newMode)
{
    // Tear down the old placement
    if (mixerWindow != nullptr)
    {
        appProperties.getUserSettings()->setValue ("mixerWindowState", mixerWindow->getWindowStateAsString());
        mixerWindow->clearContentComponent();
        mixerWindow = nullptr;
    }

    if (mixer != nullptr && mixer->getParentComponent() == this)
        removeChildComponent (mixer.get());

    if (dockDivider != nullptr)
    {
        removeChildComponent (dockDivider.get());
        dockDivider = nullptr;
    }

    mixerMode = newMode;
    appProperties.getUserSettings()->setValue ("mixerMode", (int) newMode);

    if (newMode == MixerPanel::Mode::hidden)
    {
        resized();
        menuItemsChanged();
        return;
    }

    createMixer();
    mixer->setMode (newMode);

    if (newMode == MixerPanel::Mode::docked)
    {
        auto divider = std::make_unique<DockDivider>();
        divider->getHeightNow = [this] { return dockedMixerHeight; };
        divider->onDrag = [this] (int h)
        {
            dockedMixerHeight = juce::jlimit (200, juce::jmax (200, getHeight() - 64 - 22 - 120), h);
            resized();
        };
        addAndMakeVisible (*divider);
        dockDivider = std::move (divider);

        addAndMakeVisible (*mixer);
    }
    else
    {
        auto w = std::make_unique<MixerWindow>();
        w->onClose = [this] { juce::MessageManager::callAsync ([sp = SafePointer<MainComponent> (this)] { if (sp) sp->setMixerMode (MixerPanel::Mode::hidden); }); };
        w->setContentNonOwned (mixer.get(), false);
        w->setLookAndFeel (&lookAndFeel);
        w->setIcon (Logo::createIconImage (256));

        auto saved = appProperties.getUserSettings()->getValue ("mixerWindowState");
        if (saved.isEmpty() || ! w->restoreWindowStateFromString (saved))
            w->centreWithSize (1100, 560);

        w->setVisible (true);
        mixerWindow = std::move (w);
    }

    resized();
    menuItemsChanged();
}

void MainComponent::setStatus (const juce::String& s)
{
    statusBar.setText ("  " + s, juce::dontSendNotification);
}

//==============================================================================
void MainComponent::openSession (const juce::File& file)
{
    auto restoreMixer = mixerMode;
    setMixerMode (MixerPanel::Mode::hidden);
    destroyMixer();
    arrange = nullptr;

    if (session != nullptr)
        session->save();

    auto newSession = std::make_unique<Session> (engine, file);
    session = std::move (newSession);

    timeline.selectedTrack = {};
    timeline.selectedClip = {};
    timeline.viewStart = 0;

    if (transport == nullptr)
    {
        transport = std::make_unique<TransportBar> (*session);
        transport->onRecord = [this] { toggleRecord(); };
        addAndMakeVisible (*transport);
    }
    else
    {
        transport->setSession (*session);
    }

    arrange = std::make_unique<ArrangeView> (*session, timeline);
    addAndMakeVisible (*arrange);

    session->save();
    addToRecent (session->getFile());
    appProperties.getUserSettings()->setValue ("lastSession", session->getFile().getFullPathName());

    if (auto w = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
        w->setName ("Croft DAW - " + session->getName());

    menuItemsChanged();
    resized();

    if (restoreMixer != MixerPanel::Mode::hidden)
        setMixerMode (restoreMixer);

    setStatus ("Opened " + session->getFile().getFullPathName());
}

bool MainComponent::saveBeforeQuit()
{
    appProperties.getUserSettings()->setValue ("mixerHeight", dockedMixerHeight);

    if (mixerWindow != nullptr)
        appProperties.getUserSettings()->setValue ("mixerWindowState", mixerWindow->getWindowStateAsString());

    appProperties.saveIfNeeded();

    if (session != nullptr)
    {
        session->edit->getTransport().stop (false, true);
        session->save();
    }

    return true;
}

void MainComponent::setMixerModeByName (const juce::String& name)
{
    if (name == "hidden")      setMixerMode (MixerPanel::Mode::hidden);
    else if (name == "window") setMixerMode (MixerPanel::Mode::window);
    else                       setMixerMode (MixerPanel::Mode::docked);
}

void MainComponent::saveScreenshot (const juce::File& f)
{
    auto write = [] (const juce::Image& img, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (img, out);
    };

    write (createComponentSnapshot (getLocalBounds(), true, 1.0f), f);

    // When the mixer is in its own window, save that too: <name>-mixer.png
    if (mixerMode == MixerPanel::Mode::window && mixer != nullptr)
        write (mixer->createComponentSnapshot (mixer->getLocalBounds(), true, 1.0f),
               f.getSiblingFile (f.getFileNameWithoutExtension() + "-mixer.png"));
}

void MainComponent::newSessionDialog()
{
    auto* aw = new juce::AlertWindow ("New Session", "Name your session:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", "Untitled");
    aw->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
            openSession (Session::makeNewSessionFile (aw->getTextEditorContents ("name")));
    }), true);
}

void MainComponent::openSessionDialog()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open Session", Session::getSessionsRoot(), "*" + juce::String (te::editFileSuffix));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
                openSession (fc.getResult());
        });
}

void MainComponent::saveAsDialog()
{
    auto* aw = new juce::AlertWindow ("Save Session As", "New session name:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", session->getName() + " copy");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result != 1)
            return;

        session->save();
        auto oldFolder = session->getFile().getParentDirectory();
        auto newFile = Session::makeNewSessionFile (aw->getTextEditorContents ("name"));

        // Copy the whole folder (takes included) so the copy is self-contained.
        oldFolder.copyDirectoryTo (newFile.getParentDirectory());
        auto copiedEdit = newFile.getParentDirectory().getChildFile (session->getFile().getFileName());
        copiedEdit.moveFileTo (newFile);
        openSession (newFile);
    }), true);
}

void MainComponent::exportStemsDialog()
{
    auto defaultDir = session->getFile().getParentDirectory().getChildFile ("Stems");
    auto chooser = std::make_shared<juce::FileChooser> ("Export Stems To Folder", defaultDir);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                            | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, defaultDir] (const juce::FileChooser& fc)
        {
            auto dir = fc.getResult();

            if (dir == juce::File())
                return;

            if (dir.existsAsFile())
                dir = dir.getParentDirectory();

            if (! exportStemsTo (dir).isEmpty())
                dir.revealToUser();
        });
}

void MainComponent::runRecordingSelfTest (std::function<void (bool)> onDone)
{
    auto log = [] (const juce::String& m) { std::cout << "[record-test] " << m << std::endl; };
    auto& dm = engine.getDeviceManager().deviceManager;

    // Prefer JACK when present (CI / headless), with two inputs enabled
    if (dm.getCurrentAudioDeviceType() != "JACK")
        dm.setCurrentAudioDeviceType ("JACK", true);

    auto setup = dm.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.setRange (0, 2, true);
    setup.useDefaultOutputChannels = true;
    auto err = dm.setAudioDeviceSetup (setup, true);
    log ("device: " + dm.getCurrentAudioDeviceType() + " / "
         + (dm.getCurrentAudioDevice() != nullptr ? dm.getCurrentAudioDevice()->getName() : juce::String ("none"))
         + (err.isNotEmpty() ? " error: " + err : juce::String()));

    struct Step { int delayMs; std::function<void()> fn; };
    auto steps = std::make_shared<std::vector<Step>>();
    auto ok = std::make_shared<bool> (true);
    auto sp = SafePointer<MainComponent> (this);

    auto track = [sp]() -> te::AudioTrack* { return sp != nullptr ? sp->session->getRecordTracks().getFirst() : nullptr; };

    auto describe = [sp, track, log]
    {
        auto t = track();
        juce::String clips;
        for (auto c : t->getClips())
            clips << "[" << juce::String (c->getPosition().getStart().inSeconds(), 2) << "-" << juce::String (c->getPosition().getEnd().inSeconds(), 2) << "] ";
        log ("track clips: " + clips + "| take lanes: " + juce::String (sp->session->getTakeLanes (*t).size()));
    };

    auto recordFor = [sp] (int ms)
    {
        auto& tc = sp->session->edit->getTransport();
        tc.setPosition (te::TimePosition());
        tc.record (false);
        juce::ignoreUnused (ms);
    };

    // Let the device list settle (splitting inputs into mono channels rebuilds it)
    steps->push_back ({ 1500, [sp] { Session::configureInputDevices (sp->engine); } });

    steps->push_back ({ 2500, [sp, track, log, ok]
    {
        auto t = track();
        auto inputs = sp->session->getAvailableInputs();
        log ("inputs available: " + juce::String (inputs.size()));

        te::InputDeviceInstance* wave = nullptr;
        for (auto in : inputs)
            if (in->getInputDevice().getDeviceType() == te::InputDevice::waveDevice) { wave = in; break; }

        if (wave == nullptr) { log ("FAIL: no audio input"); *ok = false; return; }

        sp->session->setInputFor (*t, wave);
        sp->session->setArmed (*t, true);
        log ("armed " + t->getName() + " on " + wave->getInputDevice().getName() + ": " + juce::String (sp->session->isArmed (*t) ? "yes" : "no"));
    }});

    steps->push_back ({ 1000, [recordFor] { recordFor (0); } });
    steps->push_back ({ 2500, [sp] { sp->session->edit->getTransport().stop (false, false); } });
    steps->push_back ({ 3000, [describe, track, ok, log]
    {
        describe();
        if (track()->getClips().size() != 1) { log ("FAIL: expected 1 clip after first pass"); *ok = false; }
    }});

    steps->push_back ({ 200, [recordFor] { recordFor (0); } });
    steps->push_back ({ 2500, [sp] { sp->session->edit->getTransport().stop (false, false); } });
    steps->push_back ({ 1500, [sp, describe, track, ok, log]
    {
        describe();
        if (sp->session->getTakeLanes (*track()).size() != 2) { log ("FAIL: expected 2 take lanes after recording over pass 1"); *ok = false; }
    }});

    // Loop record: a 2.5 s loop for ~3 passes -> several takes in one go
    steps->push_back ({ 200, [sp]
    {
        auto& tc = sp->session->edit->getTransport();
        tc.setLoopRange ({ te::TimePosition::fromSeconds (4.0), te::TimePosition::fromSeconds (6.5) });
        tc.looping = true;
        tc.setPosition (te::TimePosition::fromSeconds (4.0));
        tc.record (false);
    }});
    steps->push_back ({ 8200, [sp] { sp->session->edit->getTransport().stop (false, false); } });
    steps->push_back ({ 2000, [sp, describe, track, ok, log]
    {
        describe();
        auto lanes = sp->session->getTakeLanes (*track()).size();
        log ("lanes after loop recording: " + juce::String (lanes));
        if (lanes < 5) { log ("FAIL: loop recording should add a lane per pass"); *ok = false; }
    }});

    // Run the steps one after another
    auto runNext = std::make_shared<std::function<void (size_t)>>();
    *runNext = [steps, runNext, ok, onDone, log] (size_t i)
    {
        if (i >= steps->size())
        {
            log (*ok ? "PASS" : "FAILED");
            onDone (*ok);
            return;
        }

        juce::Timer::callAfterDelay ((*steps)[i].delayMs, [steps, runNext, i]
        {
            (*steps)[i].fn();
            (*runNext) (i + 1);
        });
    };

    (*runNext) (0);
}

juce::Array<juce::File> MainComponent::exportStemsTo (const juce::File& dir)
{
    setStatus ("Exporting stems...");
    auto files = session->exportStems (dir);

    if (files.isEmpty())
        setStatus ("Nothing to export: record or import something first.");
    else
        setStatus ("Exported " + juce::String (files.size()) + " stems (24-bit WAV, all start at 0:00) to " + dir.getFullPathName());

    return files;
}

void MainComponent::showAudioSettings()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Audio & MIDI Settings";
    o.dialogBackgroundColour = Theme::bgPanel;
    o.content.setOwned (new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager,
                                                                0, 256, 2, 256, true, false, false, false));
    o.content->setSize (520, 620);
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;
    o.launchAsync();
}

void MainComponent::showPluginManager()
{
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Plugin Manager";
    o.dialogBackgroundColour = Theme::bgPanel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.componentToCentreAround = this;

    auto& pm = engine.getPluginManager();
    auto* list = new juce::PluginListComponent (pm.pluginFormatManager, pm.knownPluginList,
                                                engine.getTemporaryFileManager().getTempFile ("PluginScanDeadMansPedal"),
                                                std::addressof (engine.getPropertyStorage().getPropertiesFile()),
                                                true);
    list->setSize (820, 600);
    o.content.setOwned (list);
    o.launchAsync();

    setStatus ("Use 'Options...' > 'Scan for new or updated VST3 plug-ins' to find EZdrummer, amp sims, etc.");
}

void MainComponent::addInstrumentTrackMenu()
{
    juce::Array<juce::PluginDescription> instruments;

    for (auto& d : engine.getPluginManager().knownPluginList.getTypes())
        if (d.isInstrument)
            instruments.add (d);

    if (instruments.isEmpty())
    {
        setStatus ("No instrument plugins found. Open Plugins > Plugin Manager and scan first.");
        showPluginManager();
        return;
    }

    juce::PopupMenu m;
    juce::KnownPluginList::addToMenu (m, instruments, juce::KnownPluginList::sortByManufacturer);

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (transport.get()),
        [this, instruments] (int result)
        {
            auto index = juce::KnownPluginList::getIndexChosenByMenu (instruments, result);

            if (index < 0)
                return;

            session->edit->getUndoManager().beginNewTransaction();

            if (auto t = session->addInstrumentTrack (instruments[index]))
            {
                timeline.selectedTrack = t->itemID;
                timeline.sendChangeMessage();

                for (auto p : session->getInsertPlugins (*t))
                    p->showWindowExplicitly();

                setStatus ("Added " + t->getName() + ". Drag grooves from the plugin onto its lane, or double-click the lane for an empty MIDI clip.");
            }
        });
}

void MainComponent::importFileDialog()
{
    auto track = getSelectedTrack();

    if (track == nullptr)
    {
        setStatus ("Select a track first.");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser> ("Import Audio or MIDI", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser, trackRef = te::Track::Ptr (track)] (const juce::FileChooser& fc)
        {
            if (auto t = dynamic_cast<te::AudioTrack*> (trackRef.get()))
                if (fc.getResult().existsAsFile())
                    session->importFile (*t, fc.getResult(), session->edit->getTransport().getPosition());
        });
}

te::AudioTrack* MainComponent::getSelectedTrack() const
{
    for (auto t : session->getTracks())
        if (t->itemID == timeline.selectedTrack)
            return t;

    return nullptr;
}

void MainComponent::deleteSelectedClip()
{
    if (! timeline.selectedClip.isValid())
        return;

    if (auto clip = te::findClipForID (*session->edit, timeline.selectedClip))
    {
        session->edit->getUndoManager().beginNewTransaction();
        clip->removeFromParent();
    }

    timeline.selectedClip = {};
    timeline.sendChangeMessage();
}

void MainComponent::splitSelectedAtPlayhead()
{
    auto pos = session->edit->getTransport().getPosition();
    session->edit->getUndoManager().beginNewTransaction();

    if (auto clip = te::findClipForID (*session->edit, timeline.selectedClip))
    {
        if (auto ct = clip->getClipTrack())
            if (clip->getPosition().time.contains (pos))
                ct->splitClip (*clip, pos);
    }
    else if (auto t = getSelectedTrack())
    {
        for (auto c : t->getClips())
            if (c->getPosition().time.contains (pos) && c->getPosition().getStart() != pos)
                t->splitClip (*c, pos);
    }
}

void MainComponent::toggleRecord()
{
    auto& t = session->edit->getTransport();

    if (t.isRecording())
    {
        t.stop (false, false);
        session->save();
        setStatus ("Recording stopped. Takes saved in " + session->getFile().getParentDirectory().getFullPathName());
        return;
    }

    bool anyArmed = false;

    for (auto track : session->getTracks())
        if (session->isArmed (*track))
            anyArmed = true;

    if (! anyArmed)
    {
        setStatus ("Arm a track first (the R button), and make sure it has an input selected.");
        return;
    }

    session->edit->getUndoManager().beginNewTransaction();
    t.record (false);
    setStatus ("Recording...");
}

//==============================================================================
bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    auto& t = session->edit->getTransport();
    const auto cmd = juce::ModifierKeys::commandModifier;

    if (k == juce::KeyPress::spaceKey)
    {
        if (t.isPlaying()) t.stop (false, false); else t.play (false);
        if (! t.isPlaying()) session->save();
        return true;
    }
    if (k == juce::KeyPress ('r') || k == juce::KeyPress ('*'))         { toggleRecord(); return true; }
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::homeKey) { t.setPosition (te::TimePosition()); return true; }
    if (k == juce::KeyPress ('x'))
    {
        setMixerMode (mixerMode == MixerPanel::Mode::hidden ? MixerPanel::Mode::docked : MixerPanel::Mode::hidden);
        return true;
    }
    if (k == juce::KeyPress ('X', juce::ModifierKeys::shiftModifier, 0) || k == juce::KeyPress ('x', juce::ModifierKeys::shiftModifier, 0))
    {
        setMixerMode (mixerMode == MixerPanel::Mode::window ? MixerPanel::Mode::docked : MixerPanel::Mode::window);
        return true;
    }
    if (k == juce::KeyPress ('l'))                                       { menuItemSelected (cmd_toggleLoop, 0); return true; }
    if (k == juce::KeyPress ('c'))                                       { menuItemSelected (cmd_toggleClick, 0); return true; }
    if (k == juce::KeyPress ('s'))                                       { splitSelectedAtPlayhead(); return true; }
    if (k == juce::KeyPress ('=') || k == juce::KeyPress ('+'))         { arrange->zoomBy (1.25); return true; }
    if (k == juce::KeyPress ('-'))                                       { arrange->zoomBy (0.8); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelectedClip(); return true; }

    if (k == juce::KeyPress ('z', cmd, 0))                               { session->edit->getUndoManager().undo(); return true; }
    if (k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)
        || k == juce::KeyPress ('y', cmd, 0))                            { session->edit->getUndoManager().redo(); return true; }
    if (k == juce::KeyPress ('s', cmd, 0))                               { session->save(); setStatus ("Saved."); return true; }
    if (k == juce::KeyPress ('n', cmd, 0))                               { newSessionDialog(); return true; }
    if (k == juce::KeyPress ('o', cmd, 0))                               { openSessionDialog(); return true; }
    if (k == juce::KeyPress ('e', cmd, 0))                               { exportStemsDialog(); return true; }
    if (k == juce::KeyPress ('t', cmd, 0))                               { menuItemSelected (cmd_addTrack, 0); return true; }
    if (k == juce::KeyPress ('i', cmd, 0))                               { importFileDialog(); return true; }
    if (k == juce::KeyPress (',', cmd, 0))                               { showAudioSettings(); return true; }

    return false;
}

//==============================================================================
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Transport", "Plugins", "View" };
}

static juce::PopupMenu::Item item (int id, const juce::String& text, const juce::String& shortcut = {}, bool enabled = true, bool ticked = false)
{
    juce::PopupMenu::Item i (text);
    i.itemID = id;
    i.isEnabled = enabled;
    i.isTicked = ticked;
    i.shortcutKeyDescription = shortcut;
    return i;
}

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
   #if JUCE_MAC
    const juce::String c = juce::String::fromUTF8 ("\xe2\x8c\x98");   // ⌘
   #else
    const juce::String c = "Ctrl+";
   #endif

    if (index == 0)
    {
        m.addItem (item (cmd_newSession, "New Session...", c + "N"));
        m.addItem (item (cmd_openSession, "Open Session...", c + "O"));

        juce::PopupMenu recent;
        auto recents = getRecentSessions();
        for (int i = 0; i < recents.size(); ++i)
            recent.addItem (cmd_recentBase + i, juce::File (recents[i]).getFileNameWithoutExtension());
        m.addSubMenu ("Open Recent", recent, ! recents.isEmpty());

        m.addSeparator();
        m.addItem (item (cmd_saveSession, "Save", c + "S"));
        m.addItem (item (cmd_saveSessionAs, "Save As..."));
        m.addSeparator();
        m.addItem (item (cmd_exportStems, "Export Stems...", c + "E"));
        m.addItem (item (cmd_revealSession, "Show Session Folder"));
        m.addSeparator();
        m.addItem (item (cmd_audioSettings, "Audio & MIDI Settings...", c + ","));
    }
    else if (index == 1)
    {
        auto& um = session->edit->getUndoManager();
        m.addItem (item (cmd_undo, "Undo " + um.getUndoDescription(), c + "Z", um.canUndo()));
        m.addItem (item (cmd_redo, "Redo " + um.getRedoDescription(), c + "Shift+Z", um.canRedo()));
        m.addSeparator();
        m.addItem (item (cmd_splitAtPlayhead, "Split at Playhead", "S"));
        m.addItem (item (cmd_deleteSelection, "Delete Clip", "Delete", timeline.selectedClip.isValid()));
        m.addSeparator();
        m.addItem (item (cmd_toggleSnap, "Snap to Beat", {}, true, timeline.snapToBeats));
    }
    else if (index == 2)
    {
        m.addItem (item (cmd_addTrack, "New Audio Track", c + "T"));
        m.addItem (item (cmd_addInstrumentTrack, "New Instrument Track (EZdrummer etc.)..."));
        m.addItem (item (cmd_addBus, "New Aux Bus (reverb, delay, group...)"));
        m.addSeparator();
        m.addItem (item (cmd_importFile, "Import Audio/MIDI to Selected Track...", c + "I", getSelectedTrack() != nullptr));
        m.addSeparator();
        m.addItem (item (cmd_deleteTrack, "Delete Selected Track", {}, getSelectedTrack() != nullptr));
    }
    else if (index == 3)
    {
        auto& t = session->edit->getTransport();
        m.addItem (item (cmd_play, t.isPlaying() ? "Stop" : "Play", "Space"));
        m.addItem (item (cmd_record, t.isRecording() ? "Stop Recording" : "Record", "R"));
        m.addItem (item (cmd_returnToStart, "Return to Start", "Enter"));
        m.addSeparator();
        m.addItem (item (cmd_toggleLoop, "Loop", "L", true, t.looping.get()));
        m.addItem (item (cmd_toggleClick, "Click", "C", true, session->edit->clickTrackEnabled.get()));
    }
    else if (index == 4)
    {
        m.addItem (item (cmd_pluginManager, "Plugin Manager / Scan..."));
    }
    else if (index == 5)
    {
        m.addItem (item (cmd_mixerDocked, "Mixer Below Tracks", "X", true, mixerMode == MixerPanel::Mode::docked));
        m.addItem (item (cmd_mixerWindowed, "Mixer in Separate Window", "Shift+X", true, mixerMode == MixerPanel::Mode::window));
        m.addItem (item (cmd_mixerHidden, "Hide Mixer", {}, true, mixerMode == MixerPanel::Mode::hidden));
        m.addSeparator();
        m.addItem (item (cmd_zoomIn, "Zoom In", "+"));
        m.addItem (item (cmd_zoomOut, "Zoom Out", "-"));
    }

    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    auto& t = session->edit->getTransport();

    if (id >= cmd_recentBase)
    {
        auto recents = getRecentSessions();
        auto f = juce::File (recents[id - cmd_recentBase]);
        if (f.existsAsFile()) openSession (f);
        return;
    }

    switch (id)
    {
        case cmd_newSession:        newSessionDialog(); break;
        case cmd_openSession:       openSessionDialog(); break;
        case cmd_saveSession:       session->save(); setStatus ("Saved."); break;
        case cmd_saveSessionAs:     saveAsDialog(); break;
        case cmd_exportStems:       exportStemsDialog(); break;
        case cmd_revealSession:     session->getFile().revealToUser(); break;
        case cmd_audioSettings:     showAudioSettings(); break;
        case cmd_undo:              session->edit->getUndoManager().undo(); break;
        case cmd_redo:              session->edit->getUndoManager().redo(); break;
        case cmd_splitAtPlayhead:   splitSelectedAtPlayhead(); break;
        case cmd_deleteSelection:   deleteSelectedClip(); break;
        case cmd_toggleSnap:        timeline.snapToBeats = ! timeline.snapToBeats; timeline.sendChangeMessage(); break;
        case cmd_addTrack:
            session->edit->getUndoManager().beginNewTransaction();
            if (session->addAudioTrack() == nullptr) setStatus ("Track limit reached.");
            break;
        case cmd_addInstrumentTrack: addInstrumentTrackMenu(); break;
        case cmd_importFile:        importFileDialog(); break;
        case cmd_deleteTrack:
            if (auto st = getSelectedTrack())
            {
                session->edit->getUndoManager().beginNewTransaction();
                session->deleteTrack (*st);
                timeline.selectedTrack = {};
            }
            break;
        case cmd_play:              if (t.isPlaying()) t.stop (false, false); else t.play (false); break;
        case cmd_record:            toggleRecord(); break;
        case cmd_returnToStart:     t.setPosition (te::TimePosition()); break;
        case cmd_toggleLoop:
            if (t.getLoopRange().isEmpty())
                t.setLoopRange ({ te::TimePosition(), session->edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)) });
            t.looping = ! t.looping.get();
            break;
        case cmd_toggleClick:       session->edit->clickTrackEnabled = ! session->edit->clickTrackEnabled.get(); break;
        case cmd_pluginManager:     showPluginManager(); break;
        case cmd_mixerDocked:   setMixerMode (MixerPanel::Mode::docked); break;
        case cmd_mixerWindowed: setMixerMode (MixerPanel::Mode::window); break;
        case cmd_mixerHidden:   setMixerMode (MixerPanel::Mode::hidden); break;
        case cmd_addBus:
            session->edit->getUndoManager().beginNewTransaction();
            if (session->addBus() == nullptr) setStatus ("Bus limit reached.");
            else if (mixerMode == MixerPanel::Mode::hidden) setMixerMode (MixerPanel::Mode::docked);
            break;
        case cmd_zoomIn:            arrange->zoomBy (1.25); break;
        case cmd_zoomOut:           arrange->zoomBy (0.8); break;
        default: break;
    }
}

//==============================================================================
juce::StringArray MainComponent::getRecentSessions()
{
    juce::StringArray list;
    list.addTokens (appProperties.getUserSettings()->getValue ("recentSessions"), "\n", {});
    list.removeEmptyStrings();
    return list;
}

void MainComponent::addToRecent (const juce::File& f)
{
    auto list = getRecentSessions();
    list.removeString (f.getFullPathName());
    list.insert (0, f.getFullPathName());

    while (list.size() > 10)
        list.remove (list.size() - 1);

    appProperties.getUserSettings()->setValue ("recentSessions", list.joinIntoString ("\n"));
    appProperties.saveIfNeeded();
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // Audio device changed (new interface, sample rate, enabled channels...)
    Session::configureInputDevices (engine);

    juce::Component::SafePointer<MainComponent> sp (this);
    juce::Timer::callAfterDelay (200, [sp]
    {
        if (sp != nullptr && sp->arrange != nullptr)
            sp->arrange->refreshInputs();
    });
}
