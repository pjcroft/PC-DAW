#pragma once

#include <JuceHeader.h>
#include "Session.h"
#include "Theme.h"
#include "TrackComponents.h"

//==============================================================================
/** Vertical stereo meter fed from a LevelMeasurer. */
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter();
    ~LevelMeter() override;

    /** Attach to a measurer (nullptr detaches). The measurer must outlive the attachment. */
    void setMeasurer (te::LevelMeasurer*);

    /** Optional dB offset added to readings (e.g. a fader that comes after the measuring point). */
    std::function<float()> getOffsetDb;
    void paint (juce::Graphics&) override;

private:
    te::LevelMeasurer* measurer = nullptr;
    te::LevelMeasurer::Client client;
    float levels[2] { -100.0f, -100.0f }, peaks[2] { -100.0f, -100.0f };
    int peakHold[2] {};

    void timerCallback() override;
};

//==============================================================================
/** One mixer channel: inserts, sends, output, pan, fader, meter. */
class ChannelStrip : public juce::Component,
                     private juce::Timer
{
public:
    static constexpr int stripWidth = 104;

    ChannelStrip (Session&, te::AudioTrack&, TimelineState&);
    ~ChannelStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    int getMinimumHeight() const;

    te::Track::Ptr trackRef;
    te::AudioTrack& track;

private:
    Session& session;
    TimelineState& state;
    const bool isBus;

    juce::Label nameLabel;
    juce::ComboBox inputBox, outputBox;
    juce::StringArray inputNames;
    juce::Array<te::EditItemID> outputIDs;

    struct InsertSlot : public juce::TextButton { int index = -1; };
    juce::OwnedArray<InsertSlot> insertSlots;
    juce::TextButton addInsertButton { "+" };

    struct SendRow : public juce::Component
    {
        SendRow (ChannelStrip&, int busNumber);
        void resized() override;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        ChannelStrip& owner;
        int busNumber;
        juce::Slider level { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    };
    juce::OwnedArray<SendRow> sendRows;
    juce::TextButton addSendButton { "+ Send" };

    juce::TextButton armButton { "R" }, monButton { "I" }, muteButton { "M" }, soloButton { "S" };
    juce::Slider pan { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    LevelMeter meter;

    juce::String lastStructureKey;

    void rebuildInserts();
    void rebuildSends();
    void refreshInputs();
    void refreshOutputs();
    juce::String structureKey() const;
    void showInsertMenu (int slotIndex);
    void showAddSendMenu();
    void timerCallback() override;

    friend struct SendRow;
};

//==============================================================================
class MasterStrip : public juce::Component,
                    private juce::Timer
{
public:
    explicit MasterStrip (Session&);
    ~MasterStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Session& session;
    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Label dbLabel;
    LevelMeter meter;
    te::Plugin::Ptr meterPlugin;   // a level meter at the end of the master plugin chain

    void timerCallback() override;
};

//==============================================================================
/** The whole mixer: a scrolling row of channel strips, buses, and the master. */
class MixerPanel : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::AsyncUpdater
{
public:
    enum class Mode { hidden, docked, window };

    MixerPanel (Session&, TimelineState&);
    ~MixerPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void rebuild();

    std::function<void()> onPopOut, onDock, onHide;
    void setMode (Mode);

private:
    Session& session;
    TimelineState& state;
    Mode mode = Mode::docked;

    juce::TextButton popButton { "Pop Out" }, hideButton { "Hide" }, addBusButton { "+ Bus" };
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<ChannelStrip> strips;
    std::unique_ptr<MasterStrip> master;

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& c) override         { if (te::TrackList::isTrack (c)) triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& c, int) override  { if (te::TrackList::isTrack (c)) triggerAsyncUpdate(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override             { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& id) override;
    void handleAsyncUpdate() override                                                 { rebuild(); }
};
