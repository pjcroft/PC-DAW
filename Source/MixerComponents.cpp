#include "MixerComponents.h"

//==============================================================================
//  LevelMeter
//==============================================================================
LevelMeter::LevelMeter()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

LevelMeter::~LevelMeter()
{
    setMeasurer (nullptr);
}

void LevelMeter::setMeasurer (te::LevelMeasurer* m)
{
    if (m == measurer)
        return;

    if (measurer != nullptr)
        measurer->removeClient (client);

    measurer = m;

    if (measurer != nullptr)
        measurer->addClient (client);
}

void LevelMeter::timerCallback()
{
    if (measurer == nullptr)
        return;

    const float offset = getOffsetDb != nullptr ? getOffsetDb() : 0.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto db = client.getAndClearAudioLevel (ch).dB + offset;
        levels[ch] = juce::jmax (db, levels[ch] - 1.2f);

        if (db >= peaks[ch]) { peaks[ch] = db; peakHold[ch] = 45; }
        else if (--peakHold[ch] <= 0) peaks[ch] = juce::jmax (-100.0f, peaks[ch] - 0.8f);
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Theme::bgDeep);
    g.fillRoundedRectangle (r, 2.0f);

    auto toY = [&r] (float db)
    {
        auto norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
        return r.getBottom() - norm * r.getHeight();
    };

    auto inner = r.reduced (1.5f);
    auto halfW = (inner.getWidth() - 1.0f) / 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (inner.getX() + (float) ch * (halfW + 1.0f), inner.getY(), halfW, inner.getHeight());
        juce::ColourGradient grad (Theme::accent, 0, r.getBottom(), Theme::record, 0, r.getY(), false);
        grad.addColour (1.0 - 6.0 / 66.0 - 0.05, Theme::accent.brighter (0.2f));
        grad.addColour (1.0 - 6.0 / 66.0, Theme::mute);
        g.setGradientFill (grad);
        g.fillRect (bar.withTop (toY (levels[ch])));

        g.setColour (peaks[ch] > 0.0f ? Theme::record : Theme::textBright);
        g.fillRect (bar.withY (toY (peaks[ch])).withHeight (1.5f));
    }

    // 0 dB tick
    g.setColour (Theme::chromeMid.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (toY (0.0f)), r.getX(), r.getRight());
}

//==============================================================================
//  ChannelStrip
//==============================================================================
ChannelStrip::ChannelStrip (Session& s, te::AudioTrack& t, TimelineState& st)
    : trackRef (&t), track (t), session (s), state (st), isBus (Session::isBus (t))
{
    nameLabel.setText (track.getName(), juce::dontSendNotification);
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    nameLabel.setEditable (false, true, false);
    nameLabel.onTextChange = [this]
    {
        track.setName (nameLabel.getText());
        if (isBus) session.edit->setAuxBusName (Session::getBusNumber (track), nameLabel.getText());
    };
    addAndMakeVisible (nameLabel);

    inputBox.setWantsKeyboardFocus (false);
    inputBox.setTooltip ("Input");
    inputBox.onChange = [this]
    {
        te::InputDeviceInstance* chosen = nullptr;
        auto id = inputBox.getSelectedId();

        if (id >= 2)
            for (auto in : session.getAvailableInputs())
                if (in->getInputDevice().getName() == inputNames[id - 2])
                    chosen = in;

        session.setInputFor (track, chosen);
    };

    if (! isBus)
        addAndMakeVisible (inputBox);

    outputBox.setWantsKeyboardFocus (false);
    outputBox.setTooltip ("Output: master, or a bus (submix)");
    outputBox.onChange = [this]
    {
        auto index = outputBox.getSelectedId() - 1;
        te::AudioTrack* dest = nullptr;

        if (juce::isPositiveAndBelow (index, outputIDs.size()) && outputIDs[index].isValid())
            dest = te::findAudioTrackForID (*session.edit, outputIDs[index]);

        session.setOutputBus (track, dest);
    };
    addAndMakeVisible (outputBox);

    addInsertButton.setTooltip ("Add a plugin");
    addInsertButton.setWantsKeyboardFocus (false);
    addInsertButton.onClick = [this] { showInsertMenu (-1); };
    addAndMakeVisible (addInsertButton);

    addSendButton.setTooltip ("Send this channel to a bus (post-fader)");
    addSendButton.setWantsKeyboardFocus (false);
    addSendButton.onClick = [this] { showAddSendMenu(); };
    addAndMakeVisible (addSendButton);

    styleToggle (armButton,  Theme::record,  "Record arm");
    styleToggle (monButton,  Theme::monitor, "Input monitoring (on = always)");
    styleToggle (muteButton, Theme::mute,    "Mute");
    styleToggle (soloButton, Theme::solo,    "Solo");

    armButton.onClick  = [this] { session.setArmed (track, ! session.isArmed (track)); };
    monButton.onClick  = [this]
    {
        using MM = te::InputDevice::MonitorMode;
        session.setMonitorMode (track, session.getMonitorMode (track) == MM::on ? MM::automatic : MM::on);
    };
    muteButton.onClick = [this] { track.setMute (! track.isMuted (false)); };
    soloButton.onClick = [this] { track.setSolo (! track.isSolo (false)); };

    for (auto b : { &muteButton, &soloButton })
        addAndMakeVisible (b);

    if (! isBus)
        for (auto b : { &armButton, &monButton })
            addAndMakeVisible (b);

    pan.setRange (-1.0, 1.0, 0.01);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setPopupDisplayEnabled (true, true, nullptr);
    pan.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? " L" : " R");
    };
    pan.onValueChange = [this] { if (auto vp = track.getVolumePlugin()) vp->setPan ((float) pan.getValue()); };
    addAndMakeVisible (pan);

    fader.setRange (-60.0, 12.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.onValueChange = [this] { if (auto vp = track.getVolumePlugin()) vp->setVolumeDb ((float) fader.getValue()); };
    addAndMakeVisible (fader);

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    dbLabel.setColour (juce::Label::textColourId, Theme::accent);
    addAndMakeVisible (dbLabel);

    if (auto lm = track.getLevelMeterPlugin())
        meter.setMeasurer (&lm->measurer);
    addAndMakeVisible (meter);

    refreshInputs();
    refreshOutputs();
    rebuildInserts();
    rebuildSends();
    lastStructureKey = structureKey();
    timerCallback();
    startTimerHz (20);
}

ChannelStrip::~ChannelStrip()
{
    meter.setMeasurer (nullptr);
}

juce::String ChannelStrip::structureKey() const
{
    // Changes to this string mean the inserts/sends/outputs need rebuilding
    juce::String key;

    for (auto p : session.getInsertPlugins (track))
        key << p->itemID.toString() << (p->isEnabled() ? "+" : "-") << p->getName() << ";";

    for (auto p : track.pluginList.getPlugins())
        if (auto send = dynamic_cast<te::AuxSendPlugin*> (p))
            key << "s" << send->getBusNumber() << ";";

    for (auto b : session.getBuses())
        key << "b" << b->itemID.toString() << b->getName() << ";";

    key << "o" << (session.getOutputBus (track) != nullptr ? session.getOutputBus (track)->itemID.toString() : juce::String());
    return key;
}

void ChannelStrip::refreshInputs()
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
        inputBox.addItem ((dev.getDeviceType() == te::InputDevice::physicalMidiDevice ? "MIDI: " : "") + dev.getName(), id);

        if (in == current)
            selected = id;
    }

    inputBox.setSelectedId (selected, juce::dontSendNotification);
}

void ChannelStrip::refreshOutputs()
{
    outputIDs.clear();
    outputBox.clear (juce::dontSendNotification);
    outputBox.addItem ("Master", 1);
    outputIDs.add ({});

    auto current = session.getOutputBus (track);
    int selected = 1;

    for (auto b : session.getBuses())
    {
        if (b == &track)
            continue;

        outputIDs.add (b->itemID);
        outputBox.addItem ("> " + b->getName(), outputIDs.size());

        if (b == current)
            selected = outputIDs.size();
    }

    outputBox.setSelectedId (selected, juce::dontSendNotification);
}

void ChannelStrip::rebuildInserts()
{
    insertSlots.clear();
    auto plugins = session.getInsertPlugins (track);

    for (int i = 0; i < plugins.size(); ++i)
    {
        auto p = plugins[i];

        if (dynamic_cast<te::AuxReturnPlugin*> (p.get()) != nullptr)
            continue;   // a bus's return is plumbing, not an insert

        auto slot = insertSlots.add (new InsertSlot());
        slot->index = i;
        slot->setButtonText (p->getName());
        slot->setTooltip (p->getName() + (p->isEnabled() ? "" : " (bypassed)") + " - click for options");
        slot->setWantsKeyboardFocus (false);
        slot->setColour (juce::TextButton::buttonOnColourId, Theme::accent.withAlpha (0.55f));
        slot->setToggleState (p->isEnabled(), juce::dontSendNotification);
        slot->onClick = [this, i] { showInsertMenu (i); };
        addAndMakeVisible (slot);
    }

    resized();
}

void ChannelStrip::rebuildSends()
{
    sendRows.clear();

    for (auto p : track.pluginList.getPlugins())
        if (auto send = dynamic_cast<te::AuxSendPlugin*> (p))
            addAndMakeVisible (sendRows.add (new SendRow (*this, send->getBusNumber())));

    resized();
}

void ChannelStrip::showInsertMenu (int slotIndex)
{
    auto plugins = session.getInsertPlugins (track);
    auto& known = session.engine.getPluginManager().knownPluginList;

    juce::Array<juce::PluginDescription> fxTypes, instTypes;
    for (auto& d : known.getTypes())
        (d.isInstrument ? instTypes : fxTypes).add (d);

    juce::PopupMenu m, effects, instruments;
    juce::KnownPluginList::addToMenu (effects, fxTypes, juce::KnownPluginList::sortByManufacturer);
    juce::KnownPluginList::addToMenu (instruments, instTypes, juce::KnownPluginList::sortByManufacturer);

    te::Plugin::Ptr plugin = juce::isPositiveAndBelow (slotIndex, plugins.size()) ? plugins[slotIndex] : nullptr;

    if (plugin != nullptr)
    {
        m.addSectionHeader (plugin->getName());
        m.addItem (1, "Open Editor");
        m.addItem (2, "Bypass", true, ! plugin->isEnabled());
        m.addItem (3, "Move Up", slotIndex > 0);
        m.addItem (4, "Move Down", slotIndex < plugins.size() - 1);
        m.addItem (5, "Remove");
        m.addSeparator();
    }

    if (fxTypes.isEmpty() && instTypes.isEmpty())
        m.addItem (-1, "No plugins found - scan in Plugins > Plugin Manager", false);
    else
    {
        m.addSubMenu ("Add Effect", effects, ! fxTypes.isEmpty());
        m.addSubMenu ("Add Instrument", instruments, ! instTypes.isEmpty());
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotIndex >= 0 && insertSlots.size() > 0 ? (juce::Component*) this : &addInsertButton),
        [sp = SafePointer<ChannelStrip> (this), plugin, plugins, slotIndex, fxTypes, instTypes] (int result)
        {
            if (sp == nullptr || result == 0) return;
            auto& t = sp->track;

            if (plugin != nullptr && result >= 1 && result <= 5)
            {
                if (result == 1) plugin->showWindowExplicitly();
                if (result == 2) plugin->setEnabled (! plugin->isEnabled());
                if (result == 3 || result == 4)
                {
                    auto other = plugins[result == 3 ? slotIndex - 1 : slotIndex + 1];
                    auto parent = plugin->state.getParent();
                    parent.moveChild (parent.indexOf (plugin->state), parent.indexOf (other->state), &t.edit.getUndoManager());
                }
                if (result == 5) plugin->deleteFromParent();
                return;
            }

            juce::PluginDescription desc;
            auto fx = juce::KnownPluginList::getIndexChosenByMenu (fxTypes, result);
            auto inst = juce::KnownPluginList::getIndexChosenByMenu (instTypes, result);
            if (fx >= 0) desc = fxTypes[fx];
            else if (inst >= 0) desc = instTypes[inst];
            else return;

            if (auto p = sp->session.addPlugin (t, desc))
                p->showWindowExplicitly();
        });
}

void ChannelStrip::showAddSendMenu()
{
    juce::PopupMenu m;
    auto buses = session.getBuses();
    int id = 1;

    for (auto b : buses)
    {
        auto n = Session::getBusNumber (*b);
        m.addItem (id++, b->getName(), b != &track && session.getSend (track, n) == nullptr);
    }

    if (! buses.isEmpty())
        m.addSeparator();

    m.addItem (1000, "New Bus...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addSendButton),
        [sp = SafePointer<ChannelStrip> (this), buses] (int result)
        {
            if (sp == nullptr || result == 0) return;
            auto& s = sp->session;
            s.edit->getUndoManager().beginNewTransaction();

            te::AudioTrack* bus = nullptr;

            if (result == 1000) bus = s.addBus();
            else                bus = buses[result - 1];

            if (bus != nullptr)
                s.addSend (sp->track, Session::getBusNumber (*bus));
        });
}

void ChannelStrip::timerCallback()
{
    using MM = te::InputDevice::MonitorMode;

    if (! isBus)
    {
        const bool hasInput = session.getInputFor (track) != nullptr;
        armButton.setEnabled (hasInput);
        monButton.setEnabled (hasInput);
        armButton.setToggleState (session.isArmed (track), juce::dontSendNotification);
        monButton.setToggleState (session.getMonitorMode (track) == MM::on, juce::dontSendNotification);
    }

    muteButton.setToggleState (track.isMuted (false), juce::dontSendNotification);
    soloButton.setToggleState (track.isSolo (false), juce::dontSendNotification);

    if (auto vp = track.getVolumePlugin())
    {
        if (! fader.isMouseButtonDown()) fader.setValue (vp->getVolumeDb(), juce::dontSendNotification);
        if (! pan.isMouseButtonDown())   pan.setValue (vp->getPan(), juce::dontSendNotification);
    }

    auto db = fader.getValue();
    dbLabel.setText (db <= -59.9 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);

    if (! nameLabel.isBeingEdited() && nameLabel.getText() != track.getName())
        nameLabel.setText (track.getName(), juce::dontSendNotification);

    auto key = structureKey();

    if (key != lastStructureKey)
    {
        lastStructureKey = key;
        refreshOutputs();
        rebuildInserts();
        rebuildSends();
    }

    for (auto row : sendRows)
        if (auto send = session.getSend (track, row->busNumber))
            if (! row->level.isMouseButtonDown())
                row->level.setValue (send->getGainDb(), juce::dontSendNotification);
}

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    state.selectedTrack = track.itemID;
    state.sendChangeMessage();

    if (e.mods.isPopupMenu() && isBus)
    {
        juce::PopupMenu m;
        m.addItem (1, "Delete Bus");
        m.showMenuAsync ({}, [sp = SafePointer<ChannelStrip> (this)] (int r)
        {
            if (sp == nullptr || r != 1) return;
            auto& s = sp->session;
            auto* t = &sp->track;
            auto n = Session::getBusNumber (*t);

            juce::MessageManager::callAsync ([&s, t, n]
            {
                s.edit->getUndoManager().beginNewTransaction();

                for (auto other : s.getAllAudioTracks())
                {
                    s.removeSend (*other, n);
                    if (s.getOutputBus (*other) == t) s.setOutputBus (*other, nullptr);
                }

                s.deleteTrack (*t);
            });
        });
    }
}

void ChannelStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool selected = state.selectedTrack == track.itemID;

    g.setGradientFill (juce::ColourGradient (isBus ? Theme::bgPanelHi.brighter (0.05f) : Theme::bgPanelHi, 0, 0,
                                             Theme::bgPanel, 0, r.getBottom(), false));
    g.fillRect (r);

    // Colour cap
    auto colour = track.getColour();
    if (colour.isTransparent()) colour = Theme::accent;
    g.setColour (colour);
    g.fillRect (r.removeFromTop (4.0f));

    g.setColour (Theme::chromeEdge);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 1);
    }

    // Section labels
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));

    auto label = [&] (const juce::String& text, int y) { g.drawText (text, 6, y - 11, getWidth() - 12, 10, juce::Justification::centredLeft); };

    if (! insertSlots.isEmpty())
        label ("INSERTS", insertSlots.getFirst()->getY());
    else
        label ("INSERTS", addInsertButton.getY());

    label ("SENDS", sendRows.isEmpty() ? addSendButton.getY() : sendRows.getFirst()->getY());
    label ("OUTPUT", outputBox.getY());

    if (isBus)
    {
        g.setColour (Theme::chromeMid);
        g.drawText ("AUX BUS " + juce::String (Session::getBusNumber (track) + 1), 6, 26, getWidth() - 12, 14, juce::Justification::centred);
    }
}

int ChannelStrip::getMinimumHeight() const
{
    // Everything above the fader, plus 150 px of fader
    return 6 + 20 + (isBus ? 18 : 22) + 11 + insertSlots.size() * 20 + 18
             + 13 + sendRows.size() * 22 + 18 + 13 + 20 + 5 + 42 + 2 + 16 + 150;
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (5, 0);
    r.removeFromTop (6);
    nameLabel.setBounds (r.removeFromTop (20));

    if (isBus)
        r.removeFromTop (18);
    else
        inputBox.setBounds (r.removeFromTop (20)), r.removeFromTop (2);

    // Inserts
    r.removeFromTop (11);
    for (auto s : insertSlots)
    {
        s->setBounds (r.removeFromTop (18));
        r.removeFromTop (2);
    }
    addInsertButton.setBounds (r.removeFromTop (18));

    // Sends
    r.removeFromTop (13);
    for (auto row : sendRows)
        row->setBounds (r.removeFromTop (22));
    addSendButton.setBounds (r.removeFromTop (18));

    // Output
    r.removeFromTop (13);
    outputBox.setBounds (r.removeFromTop (20));
    r.removeFromTop (5);

    // Buttons on the left, pan knob on the right
    auto buttons = r.removeFromTop (42);
    pan.setBounds (buttons.removeFromRight (36).withSizeKeepingCentre (34, 34));
    buttons.removeFromRight (2);
    auto row1 = buttons.removeFromTop (20);
    muteButton.setBounds (row1.removeFromLeft (row1.getWidth() / 2).reduced (1, 0));
    soloButton.setBounds (row1.reduced (1, 0));

    if (! isBus)
    {
        buttons.removeFromTop (2);
        armButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (1, 0));
        monButton.setBounds (buttons.reduced (1, 0));
    }

    r.removeFromTop (2);
    dbLabel.setBounds (r.removeFromBottom (16));

    auto faderArea = r.reduced (0, 4);
    meter.setBounds (faderArea.removeFromRight (14));
    faderArea.removeFromRight (4);
    fader.setBounds (faderArea);
}

//==============================================================================
ChannelStrip::SendRow::SendRow (ChannelStrip& o, int bus) : owner (o), busNumber (bus)
{
    level.setRange (-60.0, 6.0, 0.1);
    level.setSkewFactorFromMidPoint (-12.0);
    level.setDoubleClickReturnValue (true, 0.0);
    level.setPopupDisplayEnabled (true, true, nullptr);
    level.textFromValueFunction = [] (double v) { return v <= -59.9 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
    level.onValueChange = [this]
    {
        if (auto send = owner.session.getSend (owner.track, busNumber))
            send->setGainDb ((float) level.getValue());
    };
    addAndMakeVisible (level);
}

void ChannelStrip::SendRow::resized()
{
    level.setBounds (getLocalBounds().removeFromRight (24));
}

void ChannelStrip::SendRow::paint (juce::Graphics& g)
{
    auto bus = owner.session.getBusForNumber (busNumber);
    g.setColour (Theme::textBright);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (bus != nullptr ? bus->getName() : "Bus " + juce::String (busNumber + 1),
                getLocalBounds().withTrimmedRight (26), juce::Justification::centredLeft, true);
}

void ChannelStrip::SendRow::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    juce::PopupMenu m;
    m.addItem (1, "Remove Send");
    m.showMenuAsync ({}, [sp = SafePointer<SendRow> (this)] (int r)
    {
        if (sp != nullptr && r == 1)
        {
            auto& strip = sp->owner;
            auto bus = sp->busNumber;
            juce::MessageManager::callAsync ([s = SafePointer<ChannelStrip> (&strip), bus]
            {
                if (s != nullptr) s->session.removeSend (s->track, bus);
            });
        }
    });
}

//==============================================================================
//  MasterStrip
//==============================================================================
MasterStrip::MasterStrip (Session& s) : session (s)
{
    fader.setRange (-60.0, 12.0, 0.1);
    fader.setSkewFactorFromMidPoint (-12.0);
    fader.setDoubleClickReturnValue (true, 0.0);
    fader.onValueChange = [this]
    {
        if (auto mv = session.edit->getMasterVolumePlugin())
            mv->setVolumeDb ((float) fader.getValue());
    };
    addAndMakeVisible (fader);

    dbLabel.setJustificationType (juce::Justification::centred);
    dbLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    dbLabel.setColour (juce::Label::textColourId, Theme::accent);
    addAndMakeVisible (dbLabel);

    // Meter the master from a plugin the Edit owns, so it stays valid when the engine
    // rebuilds its playback context (e.g. during stem export). Master plugins run before
    // the master fader, so the fader level is added to the reading.
    auto& masterList = session.edit->getMasterPluginList();

    for (auto p : masterList.getPlugins())
        if (dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr)
            meterPlugin = p;

    if (meterPlugin == nullptr)
    {
        meterPlugin = session.edit->getPluginCache().createNewPlugin (te::LevelMeterPlugin::xmlTypeName, {});
        if (meterPlugin != nullptr)
            masterList.insertPlugin (meterPlugin, -1, nullptr);
    }

    if (auto lm = dynamic_cast<te::LevelMeterPlugin*> (meterPlugin.get()))
        meter.setMeasurer (&lm->measurer);

    meter.getOffsetDb = [this]
    {
        auto mv = session.edit->getMasterVolumePlugin();
        return mv != nullptr ? mv->getVolumeDb() : 0.0f;
    };
    addAndMakeVisible (meter);

    timerCallback();
    startTimerHz (20);
}

MasterStrip::~MasterStrip()
{
    meter.setMeasurer (nullptr);
}

void MasterStrip::timerCallback()
{
    if (auto mv = session.edit->getMasterVolumePlugin())
        if (! fader.isMouseButtonDown())
            fader.setValue (mv->getVolumeDb(), juce::dontSendNotification);

    auto db = fader.getValue();
    dbLabel.setText (db <= -59.9 ? juce::String ("-inf") : juce::String (db, 1), juce::dontSendNotification);
}

void MasterStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (Theme::bgPanelHi.brighter (0.1f), 0, 0, Theme::bgPanel, 0, r.getBottom(), false));
    g.fillRect (r);

    g.setGradientFill (Theme::chromeGradient (r.removeFromTop (4.0f)));
    g.fillRect (getLocalBounds().removeFromTop (4));

    g.setColour (Theme::textBright);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MASTER", getLocalBounds().removeFromTop (30).withTrimmedTop (6), juce::Justification::centred);

    g.setColour (Theme::chromeEdge);
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void MasterStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 0);
    r.removeFromTop (34);
    dbLabel.setBounds (r.removeFromBottom (18));
    auto area = r.reduced (0, 4);
    meter.setBounds (area.removeFromRight (18));
    area.removeFromRight (6);
    fader.setBounds (area);
}

//==============================================================================
//  MixerPanel
//==============================================================================
MixerPanel::MixerPanel (Session& s, TimelineState& st) : session (s), state (st)
{
    for (auto b : { &popButton, &hideButton, &addBusButton })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    popButton.setTooltip ("Move the mixer to its own window (e.g. a second screen), or dock it back below the tracks");
    popButton.onClick = [this]
    {
        if (mode == Mode::window) { if (onDock) onDock(); }
        else                      { if (onPopOut) onPopOut(); }
    };
    hideButton.onClick = [this] { if (onHide) onHide(); };
    addBusButton.setTooltip ("Add an aux bus (reverb, delay, parallel compression, group...)");
    addBusButton.onClick = [this]
    {
        session.edit->getUndoManager().beginNewTransaction();
        session.addBus();
    };

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (false, true);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    session.edit->state.addListener (this);
    rebuild();
}

MixerPanel::~MixerPanel()
{
    session.edit->state.removeListener (this);
}

void MixerPanel::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id)
{
    // Tracks becoming buses / take lanes changes which strips exist
    if (te::TrackList::isTrack (v) && (id.toString().startsWith ("croft")))
        triggerAsyncUpdate();
}

void MixerPanel::setMode (Mode m)
{
    mode = m;
    popButton.setButtonText (mode == Mode::window ? "Dock" : "Pop Out");
    hideButton.setVisible (true);
}

void MixerPanel::rebuild()
{
    strips.clear();

    for (auto t : session.getTracks())
        stripHolder.addAndMakeVisible (strips.add (new ChannelStrip (session, *t, state)));

    if (master == nullptr)
    {
        master = std::make_unique<MasterStrip> (session);
        addAndMakeVisible (*master);
    }

    resized();
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bgDeep);
    auto bar = getLocalBounds().removeFromTop (28).toFloat();
    Theme::fillPanel (g, bar);
    g.setColour (Theme::chromeMid);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText ("MIXER", bar.reduced (10, 0), juce::Justification::centredLeft);

    g.setColour (Theme::chromeEdge);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (28).reduced (4, 3);
    hideButton.setBounds (bar.removeFromRight (60));
    bar.removeFromRight (4);
    popButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (12);
    addBusButton.setBounds (bar.removeFromRight (64));

    if (master != nullptr)
        master->setBounds (r.removeFromRight (90));

    viewport.setBounds (r);

    // Strips need room for their sections plus a usable fader; scroll vertically if the panel is short
    int needed = 0;
    for (auto s : strips)
        needed = juce::jmax (needed, s->getMinimumHeight());

    viewport.setScrollBarsShown (needed > r.getHeight(), true);
    auto h = juce::jmax (needed, viewport.getMaximumVisibleHeight());
    stripHolder.setSize (juce::jmax (viewport.getWidth(), strips.size() * ChannelStrip::stripWidth), h);

    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (i * ChannelStrip::stripWidth, 0, ChannelStrip::stripWidth, h);
}
