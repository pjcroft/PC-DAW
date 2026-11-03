#include "TrackComponents.h"

//==============================================================================
static juce::Colour trackColourFor (te::AudioTrack& t)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xff5ac8fa), juce::Colour (0xff7d8cff), juce::Colour (0xffb48cff), juce::Colour (0xffff7eb6),
        juce::Colour (0xffff9f5a), juce::Colour (0xfff2d24b), juce::Colour (0xff4be08a), juce::Colour (0xff45d4c8)
    };

    auto c = t.getColour();

    if (! c.isTransparent() && c.getAlpha() > 0)
        return c;

    auto index = te::getAudioTracks (t.edit).indexOf (&t);
    return palette[juce::jmax (0, index) % (int) std::size (palette)];
}

void styleToggle (juce::TextButton& b, juce::Colour led, const juce::String& tooltip)
{
    b.setClickingTogglesState (false);
    b.setColour (juce::TextButton::buttonOnColourId, led);
    b.setTooltip (tooltip);
    b.setWantsKeyboardFocus (false);
}

double snapTimeToBeat (te::Edit& edit, double seconds)
{
    auto beats = edit.tempoSequence.toBeats (te::TimePosition::fromSeconds (juce::jmax (0.0, seconds)));
    auto snapped = te::BeatPosition::fromBeats (std::round (beats.inBeats()));
    return edit.tempoSequence.toTime (snapped).inSeconds();
}

void drawBeatGrid (juce::Graphics& g, te::Edit& edit, const TimelineState& state, juce::Rectangle<int> area, bool forRuler)
{
    auto& tempo = edit.tempoSequence;
    const double startTime = state.viewStart;
    const double endTime   = state.xToTime (area.getWidth());

    const double secondsPerBeat = 60.0 / juce::jmax (1.0, tempo.getBpmAt (te::TimePosition::fromSeconds (startTime)));
    const double pxPerBeat = secondsPerBeat * state.pixelsPerSecond;
    const int numerator = juce::jmax (1, (int) tempo.getTimeSigAt (te::TimePosition::fromSeconds (startTime)).numerator.get());
    const double pxPerBar = pxPerBeat * numerator;

    // Label every Nth bar so numbers never collide.
    int barLabelStep = 1;
    while (pxPerBar * barLabelStep < 40.0)
        barLabelStep *= 2;

    const bool drawBeats = pxPerBeat >= 8.0;

    auto firstBeat = (int) std::floor (tempo.toBeats (te::TimePosition::fromSeconds (startTime)).inBeats());
    auto lastBeat  = (int) std::ceil  (tempo.toBeats (te::TimePosition::fromSeconds (endTime)).inBeats());

    for (int b = juce::jmax (0, firstBeat); b <= lastBeat; ++b)
    {
        auto t = tempo.toTime (te::BeatPosition::fromBeats ((double) b));
        auto x = (float) area.getX() + state.timeToX (t.inSeconds());
        auto bb = tempo.toBarsAndBeats (t);
        const bool isBar = bb.getWholeBeats() == 0;

        if (! isBar && ! drawBeats)
            continue;

        if (forRuler)
        {
            const bool labelled = isBar && (bb.bars % barLabelStep) == 0;
            g.setColour (isBar ? Theme::chromeMid : Theme::chromeLo);
            auto tickTop = (float) area.getBottom() - (isBar ? (labelled ? (float) area.getHeight() : 10.0f) : 5.0f);
            g.drawVerticalLine (juce::roundToInt (x), tickTop, (float) area.getBottom());

            if (labelled)
            {
                g.setColour (Theme::textBright);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (juce::String (bb.bars + 1), juce::roundToInt (x) + 3, area.getY(), 40, area.getHeight() - 6,
                            juce::Justification::centredLeft, false);
            }
        }
        else
        {
            g.setColour (isBar ? Theme::gridBar : Theme::gridBeat);
            g.drawVerticalLine (juce::roundToInt (x), (float) area.getY(), (float) area.getBottom());
        }
    }
}

//==============================================================================
//  TrackHeader
//==============================================================================
TrackHeader::TrackHeader (Session& s, te::AudioTrack& t, TimelineState& st)
    : trackRef (&t), track (t), session (s), state (st)
{
    nameLabel.setText (track.getName(), juce::dontSendNotification);
    nameLabel.setEditable (false, true, false);
    nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    nameLabel.setColour (juce::Label::textColourId, Theme::textBright);
    nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, Theme::bgDeep);
    nameLabel.onTextChange = [this] { track.setName (nameLabel.getText()); };
    nameLabel.setInterceptsMouseClicks (false, false);   // single click selects; double-click handled below
    addAndMakeVisible (nameLabel);

    styleToggle (armButton,  Theme::record,  "Record arm");
    styleToggle (monButton,  Theme::monitor, "Input monitoring: on = always hear input; off = hear input only while armed");
    styleToggle (muteButton, Theme::mute,    "Mute");
    styleToggle (soloButton, Theme::solo,    "Solo");
    styleToggle (fxButton,   Theme::accent,  "Plugins (inserts)");

    armButton.onClick = [this] { session.setArmed (track, ! session.isArmed (track)); };
    monButton.onClick = [this]
    {
        using MM = te::InputDevice::MonitorMode;
        session.setMonitorMode (track, session.getMonitorMode (track) == MM::on ? MM::automatic : MM::on);
    };
    muteButton.onClick = [this] { track.setMute (! track.isMuted (false)); };
    soloButton.onClick = [this] { track.setSolo (! track.isSolo (false)); };
    fxButton.onClick   = [this] { showFxMenu(); };

    for (auto b : { &armButton, &monButton, &muteButton, &soloButton, &fxButton })
        addAndMakeVisible (b);

    inputBox.setTooltip ("Input");
    inputBox.onChange = [this]
    {
        auto id = inputBox.getSelectedId();
        te::InputDeviceInstance* chosen = nullptr;

        if (id >= 2)
        {
            auto name = inputNames[id - 2];

            for (auto in : session.getAvailableInputs())
                if (in->getInputDevice().getName() == name)
                    chosen = in;
        }

        session.setInputFor (track, chosen);
    };
    addAndMakeVisible (inputBox);

    volume.setRange (-60.0, 12.0, 0.1);
    volume.setSkewFactorFromMidPoint (-12.0);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTooltip ("Volume (double-click = 0 dB)");
    volume.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    volume.setPopupDisplayEnabled (true, true, nullptr);
    volume.onValueChange = [this]
    {
        if (auto vp = track.getVolumePlugin())
            vp->setVolumeDb ((float) volume.getValue());
    };
    addAndMakeVisible (volume);

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan (double-click = centre)");
    pan.setPopupDisplayEnabled (true, true, nullptr);
    pan.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R");
    };
    pan.onValueChange = [this]
    {
        if (auto vp = track.getVolumePlugin())
            vp->setPan ((float) pan.getValue());
    };
    addAndMakeVisible (pan);

    if (auto lm = track.getLevelMeterPlugin())
        lm->measurer.addClient (meterClient);

    refreshInputs();
    timerCallback();
    startTimerHz (30);
}

TrackHeader::~TrackHeader()
{
    if (auto lm = track.getLevelMeterPlugin())
        lm->measurer.removeClient (meterClient);
}

void TrackHeader::refreshInputs()
{
    inputNames.clear();
    inputBox.clear (juce::dontSendNotification);
    inputBox.addItem ("No Input", 1);

    auto current = session.getInputFor (track);
    int selected = 1;

    for (auto in : session.getAvailableInputs())
    {
        auto& dev = in->getInputDevice();
        inputNames.add (dev.getName());
        auto id = inputNames.size() + 1;
        auto label = (dev.getDeviceType() == te::InputDevice::physicalMidiDevice ? "MIDI: " : "") + dev.getName();
        inputBox.addItem (label, id);

        if (in == current)
            selected = id;
    }

    inputBox.setSelectedId (selected, juce::dontSendNotification);
}

juce::Rectangle<int> TrackHeader::getMeterBounds() const
{
    return getLocalBounds().removeFromRight (12).reduced (3, 6);
}

void TrackHeader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool selected = state.selectedTrack == track.itemID;

    g.setGradientFill (juce::ColourGradient (selected ? Theme::bgPanelHi.brighter (0.08f) : Theme::bgPanelHi, 0, 0,
                                             Theme::bgPanel, 0, r.getBottom(), false));
    g.fillRect (r);

    // Track colour strip
    g.setColour (trackColourFor (track));
    g.fillRect (r.removeFromLeft (4.0f));

    g.setColour (Theme::chromeEdge);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (0.6f));
        g.drawRect (getLocalBounds().withTrimmedLeft (4), 1);
    }

    // Stereo meter
    auto m = getMeterBounds().toFloat();
    g.setColour (Theme::bgDeep);
    g.fillRoundedRectangle (m, 1.5f);

    auto drawBar = [&] (juce::Rectangle<float> bar, float db)
    {
        auto norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);  // -60..+6 dB
        auto filled = bar.removeFromBottom (bar.getHeight() * norm);
        juce::ColourGradient grad (Theme::accent, 0, m.getBottom(), Theme::record, 0, m.getY(), false);
        grad.addColour (0.8, Theme::mute);
        g.setGradientFill (grad);
        g.fillRect (filled);
    };

    auto left = m.reduced (1.0f);
    auto right = left.removeFromRight (left.getWidth() / 2.0f);
    drawBar (left.withTrimmedRight (0.5f), meterL);
    drawBar (right.withTrimmedLeft (0.5f), meterR);
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (8).withTrimmedRight (14).reduced (2, 4);

    auto row1 = r.removeFromTop (22);
    const int bw = 22;
    for (auto b : { &armButton, &monButton, &muteButton, &soloButton })
    {
        b->setBounds (row1.removeFromLeft (bw).reduced (1));
        row1.removeFromLeft (1);
    }
    row1.removeFromLeft (4);
    nameLabel.setBounds (row1);

    r.removeFromTop (4);
    auto row2 = r.removeFromTop (22);
    pan.setBounds (row2.removeFromRight (24));
    row2.removeFromRight (4);
    fxButton.setBounds (row2.removeFromRight (44).reduced (1));
    row2.removeFromRight (4);
    inputBox.setBounds (row2);

    r.removeFromTop (4);
    volume.setBounds (r.removeFromTop (18));
}

void TrackHeader::timerCallback()
{
    using MM = te::InputDevice::MonitorMode;

    const bool hasInput = session.getInputFor (track) != nullptr;
    armButton.setEnabled (hasInput);
    monButton.setEnabled (hasInput);
    armButton.setToggleState (session.isArmed (track), juce::dontSendNotification);
    monButton.setToggleState (session.getMonitorMode (track) == MM::on, juce::dontSendNotification);
    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    soloButton.setToggleState (track.isSolo (false), juce::dontSendNotification);

    auto numFx = session.getInsertPlugins (track).size();
    fxButton.setButtonText (numFx > 0 ? "FX " + juce::String (numFx) : juce::String ("FX"));
    fxButton.setToggleState (numFx > 0, juce::dontSendNotification);

    if (auto vp = track.getVolumePlugin())
    {
        if (! volume.isMouseButtonDown())
            volume.setValue (vp->getVolumeDb(), juce::dontSendNotification);
        if (! pan.isMouseButtonDown())
            pan.setValue (vp->getPan(), juce::dontSendNotification);
    }

    if (! nameLabel.isBeingEdited() && nameLabel.getText() != track.getName())
        nameLabel.setText (track.getName(), juce::dontSendNotification);

    // Meter with falloff
    auto l = meterClient.getAndClearAudioLevel (0).dB;
    auto rr = meterClient.getAndClearAudioLevel (1).dB;
    meterL = juce::jmax (l, meterL - 1.5f);
    meterR = juce::jmax (rr, meterR - 1.5f);
    repaint (getMeterBounds());
}

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    state.selectedTrack = track.itemID;
    state.sendChangeMessage();

    if (e.mods.isPopupMenu())
        showTrackMenu();
    else if (e.getNumberOfClicks() == 2 && nameLabel.getBounds().contains (e.getPosition()))
        nameLabel.showEditor();
}

void TrackHeader::showTrackMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addSeparator();
    m.addItem (2, "Delete Track");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [sp = SafePointer<TrackHeader> (this)] (int result)
                     {
                         if (sp == nullptr) return;
                         if (result == 1) sp->nameLabel.showEditor();
                         if (result == 2)
                         {
                             auto& sess = sp->session;
                             auto* t = &sp->track;
                             juce::MessageManager::callAsync ([&sess, t] { sess.deleteTrack (*t); });
                         }
                     });
}

void TrackHeader::showFxMenu()
{
    auto& known = session.engine.getPluginManager().knownPluginList;
    auto types = known.getTypes();
    auto plugins = session.getInsertPlugins (track);

    juce::PopupMenu m;

    for (int i = 0; i < plugins.size(); ++i)
    {
        auto p = plugins[i];
        juce::PopupMenu sub;
        sub.addItem (10000 + i * 10 + 0, "Open Editor");
        sub.addItem (10000 + i * 10 + 1, "Bypass", true, ! p->isEnabled());
        sub.addItem (10000 + i * 10 + 2, "Move Up", i > 0);
        sub.addItem (10000 + i * 10 + 3, "Move Down", i < plugins.size() - 1);
        sub.addSeparator();
        sub.addItem (10000 + i * 10 + 4, "Remove");
        m.addSubMenu (juce::String (i + 1) + ". " + p->getName() + (p->isEnabled() ? "" : "  (bypassed)"), sub);
    }

    if (! plugins.isEmpty())
        m.addSeparator();

    juce::PopupMenu effects, instruments;
    juce::Array<juce::PluginDescription> fxTypes, instTypes;

    for (auto& d : types)
        (d.isInstrument ? instTypes : fxTypes).add (d);

    juce::KnownPluginList::addToMenu (effects, fxTypes, juce::KnownPluginList::sortByManufacturer);
    juce::KnownPluginList::addToMenu (instruments, instTypes, juce::KnownPluginList::sortByManufacturer);

    if (types.isEmpty())
    {
        m.addItem (-1, "No plugins found - use Plugins > Plugin Manager to scan", false, false);
    }
    else
    {
        m.addSubMenu ("Add Effect", effects, ! fxTypes.isEmpty());
        m.addSubMenu ("Add Instrument", instruments, ! instTypes.isEmpty());
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fxButton),
        [sp = SafePointer<TrackHeader> (this), plugins, fxTypes, instTypes] (int result)
        {
            if (sp == nullptr || result == 0)
                return;

            auto& t = sp->track;

            if (result >= 10000)
            {
                auto index = (result - 10000) / 10;
                auto action = (result - 10000) % 10;
                auto p = plugins[index];

                if (p == nullptr)
                    return;

                if (action == 0)      p->showWindowExplicitly();
                else if (action == 1) p->setEnabled (! p->isEnabled());
                else if (action == 2 || action == 3)
                {
                    auto& list = t.pluginList;
                    auto current = list.indexOf (p.get());
                    auto other = list.indexOf (plugins[action == 2 ? index - 1 : index + 1].get());

                    if (current >= 0 && other >= 0)
                    {
                        auto tree = p->state;
                        auto parent = tree.getParent();
                        parent.moveChild (parent.indexOf (tree),
                                          parent.indexOf (plugins[action == 2 ? index - 1 : index + 1]->state),
                                          &t.edit.getUndoManager());
                    }
                }
                else if (action == 4) p->deleteFromParent();

                return;
            }

            // Menu ids from KnownPluginList::addToMenu
            juce::PluginDescription desc;
            auto fxIndex = juce::KnownPluginList::getIndexChosenByMenu (fxTypes, result);
            auto instIndex = juce::KnownPluginList::getIndexChosenByMenu (instTypes, result);

            if (fxIndex >= 0)        desc = fxTypes[fxIndex];
            else if (instIndex >= 0) desc = instTypes[instIndex];
            else return;

            if (auto p = sp->session.addPlugin (t, desc))
                p->showWindowExplicitly();
        });
}

//==============================================================================
//  TrackLane
//==============================================================================
TrackLane::TrackLane (Session& s, te::AudioTrack& t, TimelineState& st)
    : trackRef (&t), track (t), session (s), state (st)
{
    track.state.addListener (this);
    rebuildThumbnails();
}

TrackLane::~TrackLane()
{
    track.state.removeListener (this);
}

void TrackLane::handleAsyncUpdate()
{
    rebuildThumbnails();
    repaint();
}

void TrackLane::rebuildThumbnails()
{
    std::map<te::EditItemID, std::unique_ptr<te::SmartThumbnail>> newThumbs;

    for (auto c : track.getClips())
    {
        if (auto wac = dynamic_cast<te::WaveAudioClip*> (c))
        {
            te::AudioFile af (wac->getAudioFile());

            if (! af.isValid())
                continue;

            auto existing = thumbnails.find (c->itemID);

            if (existing != thumbnails.end())
            {
                newThumbs[c->itemID] = std::move (existing->second);
                newThumbs[c->itemID]->setNewFile (af);
            }
            else
            {
                newThumbs[c->itemID] = std::make_unique<te::SmartThumbnail> (session.engine, af, *this, &track.edit);
            }
        }
    }

    thumbnails = std::move (newThumbs);
}

juce::Rectangle<float> TrackLane::clipBounds (te::Clip& c) const
{
    auto pos = c.getPosition();
    auto x1 = state.timeToX (pos.getStart().inSeconds());
    auto x2 = state.timeToX (pos.getEnd().inSeconds());
    return { x1, 2.0f, juce::jmax (2.0f, x2 - x1), (float) getHeight() - 4.0f };
}

te::Clip* TrackLane::clipAt (int x) const
{
    auto clips = track.getClips();

    for (int i = clips.size(); --i >= 0;)
        if (clipBounds (*clips[i]).getHorizontalRange().contains ((float) x))
            return clips[i];

    return nullptr;
}

double TrackLane::snap (double seconds) const
{
    return state.snapToBeats ? snapTimeToBeat (track.edit, seconds) : juce::jmax (0.0, seconds);
}

void TrackLane::drawClip (juce::Graphics& g, te::Clip& c)
{
    auto r = clipBounds (c);

    if (r.getRight() < 0 || r.getX() > (float) getWidth())
        return;

    const bool selected = state.selectedClip == c.itemID;
    const bool isMidi = dynamic_cast<te::MidiClip*> (&c) != nullptr;
    auto base = isMidi ? Theme::clipMidi : Theme::clipAudio;
    auto tc = trackColourFor (track);
    base = base.interpolatedWith (tc, 0.25f);

    g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), 0, r.getY(), base.darker (0.25f), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 3.0f);

    // Title bar
    auto title = r.removeFromTop (14.0f);
    g.setColour (base.darker (0.4f).withAlpha (0.8f));
    g.fillRect (title);
    g.setColour (Theme::textBright);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (c.getName(), title.reduced (4.0f, 0).toNearestInt(), juce::Justification::centredLeft, true);

    auto body = r.reduced (1.0f, 2.0f).toNearestInt();

    if (auto wac = dynamic_cast<te::WaveAudioClip*> (&c))
    {
        auto it = thumbnails.find (c.itemID);
        if (it != thumbnails.end() && it->second != nullptr)
        {
            auto pos = c.getPosition();
            auto srcStart = te::TimePosition() + pos.getOffset();
            g.setColour (Theme::clipWave.withAlpha (0.85f));
            it->second->drawChannels (g, body, { srcStart, srcStart + pos.getLength() },
                                      juce::Decibels::decibelsToGain (wac->getGainDB()));
        }
    }
    else if (auto mc = dynamic_cast<te::MidiClip*> (&c))
    {
        auto& notes = mc->getSequence().getNotes();
        int lo = 127, hi = 0;

        for (auto n : notes)
        {
            lo = juce::jmin (lo, n->getNoteNumber());
            hi = juce::jmax (hi, n->getNoteNumber());
        }

        if (lo <= hi)
        {
            const int range = juce::jmax (12, hi - lo + 1);
            const float noteH = juce::jmax (1.0f, (float) body.getHeight() / (float) range);
            g.setColour (Theme::clipWave.withAlpha (0.85f));

            for (auto n : notes)
            {
                auto s = mc->getTimeOfContentBeat (n->getStartBeat()).inSeconds();
                auto e = mc->getTimeOfContentBeat (n->getEndBeat()).inSeconds();

                if (s < c.getPosition().getStart().inSeconds() || s >= c.getPosition().getEnd().inSeconds())
                    continue;

                auto x1 = state.timeToX (s);
                auto x2 = juce::jmax (x1 + 2.0f, state.timeToX (e));
                auto y = (float) body.getBottom() - (float) (n->getNoteNumber() - lo + 1) * noteH;
                g.fillRect (x1, y, x2 - x1, juce::jmax (1.5f, noteH - 1.0f));
            }
        }
    }

    auto full = clipBounds (c);
    g.setColour (selected ? Theme::accent : base.brighter (0.4f).withAlpha (0.6f));
    g.drawRoundedRectangle (full, 3.0f, selected ? 2.0f : 1.0f);
}

void TrackLane::paint (juce::Graphics& g)
{
    const auto index = te::getAudioTracks (track.edit).indexOf (&track);
    const bool selected = state.selectedTrack == track.itemID;
    g.fillAll (selected ? Theme::laneSelected : ((index % 2) == 0 ? Theme::laneEven : Theme::laneOdd));

    drawBeatGrid (g, track.edit, state, getLocalBounds(), false);

    for (auto c : track.getClips())
        drawClip (g, *c);

    // Take being recorded
    if (wasRecording && recordingThumb != nullptr)
    {
        auto now = track.edit.getTransport().getPosition().inSeconds();
        auto x1 = state.timeToX (recordingStart.inSeconds());
        auto x2 = state.timeToX (now);

        if (x2 > x1)
        {
            juce::Rectangle<float> r (x1, 2.0f, x2 - x1, (float) getHeight() - 4.0f);
            g.setColour (Theme::clipRecording.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (Theme::record);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            g.setColour (Theme::textBright.withAlpha (0.9f));
            recordingThumb->thumb->drawChannels (g, r.reduced (1.0f, 4.0f).toNearestInt(),
                                                 0.0, now - recordingStart.inSeconds(), 1.0f);
        }
    }

    g.setColour (Theme::chromeEdge.withAlpha (0.7f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    if (dropIndicatorX >= 0)
    {
        g.setColour (Theme::accent);
        g.fillRect (dropIndicatorX - 1, 0, 2, getHeight());
        g.setColour (Theme::accentGlow);
        g.fillRect (getLocalBounds());
    }
}

void TrackLane::updateRecording()
{
    auto& transport = track.edit.getTransport();
    const bool recordingNow = transport.isRecording() && session.isArmed (track);

    if (recordingNow)
    {
        if (! wasRecording || recordingThumb == nullptr)
        {
            for (auto idi : track.edit.getEditInputDevices().getDevicesForTargetTrack (track))
            {
                auto file = idi->getRecordingFile (track.itemID);

                if (file != juce::File())
                {
                    recordingThumb = session.engine.getRecordingThumbnailManager().getThumbnailFor (file);
                    recordingStart = idi->getPunchInTime (track.itemID);
                }
            }
        }

        wasRecording = true;
        repaint();
    }
    else if (wasRecording)
    {
        wasRecording = false;
        recordingThumb = nullptr;
        repaint();
    }
}

void TrackLane::mouseDown (const juce::MouseEvent& e)
{
    auto& um = track.edit.getUndoManager();
    um.beginNewTransaction();

    auto clickTime = state.xToTime (e.position.x);
    auto clip = clipAt (e.x);

    state.selectedTrack = track.itemID;
    state.selectedClip = clip != nullptr ? clip->itemID : te::EditItemID();
    state.sendChangeMessage();

    if (e.mods.isPopupMenu())
    {
        showClipMenu (clip, clickTime);
        return;
    }

    if (clip != nullptr)
    {
        draggingClip = clip;
        dragStartClipTime = clip->getPosition().getStart().inSeconds();
        dragStartMouseTime = clickTime;
    }
    else
    {
        auto& transport = track.edit.getTransport();

        if (! transport.isRecording())
            transport.setPosition (te::TimePosition::fromSeconds (snap (clickTime)));
    }
}

void TrackLane::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingClip == nullptr || e.getDistanceFromDragStart() < 3)
        return;

    auto delta = state.xToTime (e.position.x) - dragStartMouseTime;
    auto newStart = dragStartClipTime + delta;
    newStart = e.mods.isShiftDown() || ! state.snapToBeats ? juce::jmax (0.0, newStart) : snap (newStart);

    draggingClip->setStart (te::TimePosition::fromSeconds (newStart), false, true);
}

void TrackLane::mouseUp (const juce::MouseEvent&)
{
    draggingClip = nullptr;
}

void TrackLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Double-click empty space on an instrument track = new 4-bar MIDI clip
    if (clipAt (e.x) != nullptr)
        return;

    bool hasInstrument = false;

    for (auto p : session.getInsertPlugins (track))
        if (p->isSynth())
            hasInstrument = true;

    if (! hasInstrument)
        return;

    auto& tempo = track.edit.tempoSequence;
    auto start = tempo.toTime (te::BeatPosition::fromBeats (std::floor (tempo.toBeats (te::TimePosition::fromSeconds (state.xToTime (e.position.x))).inBeats() / 4.0) * 4.0));
    auto end = tempo.toTime (tempo.toBeats (start) + te::BeatDuration::fromBeats (16.0));
    track.insertMIDIClip ("MIDI", { start, end }, nullptr);
}

void TrackLane::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        state.setZoom (state.pixelsPerSecond * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15), e.position.x);
        return;
    }

    const float dx = e.mods.isShiftDown() ? w.deltaY : w.deltaX;

    if (std::abs (dx) > 0.0f)
    {
        state.setViewStart (state.viewStart - dx * 400.0 / state.pixelsPerSecond);
        return;
    }

    Component::mouseWheelMove (e, w);   // vertical scroll goes up to the viewport
}

void TrackLane::showClipMenu (te::Clip* clip, double time)
{
    juce::PopupMenu m;
    auto playhead = track.edit.getTransport().getPosition();

    if (clip != nullptr)
    {
        auto range = clip->getPosition().time;
        m.addItem (1, "Split at Playhead", range.contains (playhead) && range.getStart() != playhead);
        m.addItem (2, "Delete Clip");
        m.addSeparator();
    }

    m.addItem (3, "Import Audio/MIDI File Here...");

    m.showMenuAsync (juce::PopupMenu::Options(),
        [sp = SafePointer<TrackLane> (this), clipPtr = te::Clip::Ptr (clip), time, playhead] (int result)
        {
            if (sp == nullptr) return;

            if (result == 1 && clipPtr != nullptr)
                sp->track.splitClip (*clipPtr, playhead);
            else if (result == 2 && clipPtr != nullptr)
                clipPtr->removeFromParent();
            else if (result == 3)
            {
                auto chooser = std::make_shared<juce::FileChooser> ("Import", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
                chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                    [sp, chooser, time] (const juce::FileChooser& fc)
                    {
                        if (sp != nullptr && fc.getResult().existsAsFile())
                            sp->session.importFile (sp->track, fc.getResult(), te::TimePosition::fromSeconds (sp->snap (time)));
                    });
            }
        });
}

bool TrackLane::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi"))
            return true;

    return false;
}

void TrackLane::fileDragEnter (const juce::StringArray&, int x, int)   { dropIndicatorX = juce::roundToInt (state.timeToX (snap (state.xToTime (x)))); repaint(); }
void TrackLane::fileDragMove (const juce::StringArray& f, int x, int y) { fileDragEnter (f, x, y); }
void TrackLane::fileDragExit (const juce::StringArray&)                { dropIndicatorX = -1; repaint(); }

void TrackLane::filesDropped (const juce::StringArray& files, int x, int)
{
    dropIndicatorX = -1;
    track.edit.getUndoManager().beginNewTransaction();

    auto time = te::TimePosition::fromSeconds (snap (state.xToTime (x)));

    for (auto& f : files)
    {
        juce::File file (f);

        if (session.importFile (track, file, time))
        {
            // Place multiple files end-to-end
            if (auto last = track.getClips().getLast())
                time = last->getPosition().getEnd();
        }
    }

    repaint();
}

//==============================================================================
//  Ruler
//==============================================================================
Ruler::Ruler (Session& s, TimelineState& st) : session (s), state (st) {}

void Ruler::paint (juce::Graphics& g)
{
    Theme::fillPanel (g, getLocalBounds().toFloat());

    auto& transport = session.edit->getTransport();
    auto loop = transport.getLoopRange();

    if (! loop.isEmpty())
    {
        auto x1 = state.timeToX (loop.getStart().inSeconds());
        auto x2 = state.timeToX (loop.getEnd().inSeconds());
        auto c = transport.looping ? Theme::accent : Theme::chromeLo;
        g.setColour (c.withAlpha (0.3f));
        g.fillRect (x1, 0.0f, x2 - x1, (float) getHeight());
        g.setColour (c);
        g.fillRect (x1, 0.0f, x2 - x1, 3.0f);
    }

    drawBeatGrid (g, *session.edit, state, getLocalBounds(), true);
}

void Ruler::mouseDown (const juce::MouseEvent& e)
{
    dragStart = snapTimeToBeat (*session.edit, state.xToTime (e.position.x));
    draggingLoop = false;
}

void Ruler::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStartX() == 0 || std::abs (e.getDistanceFromDragStartX()) < 4)
        return;

    draggingLoop = true;
    auto t = snapTimeToBeat (*session.edit, state.xToTime (e.position.x));
    auto a = juce::jmin (dragStart, t), b = juce::jmax (dragStart, t);

    if (b > a)
    {
        auto& transport = session.edit->getTransport();
        transport.setLoopRange ({ te::TimePosition::fromSeconds (a), te::TimePosition::fromSeconds (b) });
        transport.looping = true;
    }

    repaint();
}

void Ruler::mouseUp (const juce::MouseEvent& e)
{
    if (! draggingLoop)
    {
        auto& transport = session.edit->getTransport();

        if (! transport.isRecording())
            transport.setPosition (te::TimePosition::fromSeconds (
                e.mods.isShiftDown() ? juce::jmax (0.0, state.xToTime (e.position.x)) : dragStart));
    }
}
