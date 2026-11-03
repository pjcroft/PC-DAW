#pragma once

#include <JuceHeader.h>
#include "Session.h"
#include "Theme.h"

//==============================================================================
/** Shared zoom / scroll / selection state for the arrange view. */
struct TimelineState : public juce::ChangeBroadcaster
{
    double pixelsPerSecond = 40.0;
    double viewStart       = 0.0;      // seconds at the left edge of the lanes
    bool   snapToBeats     = true;

    te::EditItemID selectedTrack, selectedClip;

    double xToTime (double x) const             { return viewStart + x / pixelsPerSecond; }
    float  timeToX (double t) const             { return (float) ((t - viewStart) * pixelsPerSecond); }

    void setViewStart (double s)                { s = juce::jmax (0.0, s); if (s != viewStart) { viewStart = s; sendChangeMessage(); } }
    void setZoom (double pps, double anchorX)
    {
        pps = juce::jlimit (2.0, 2000.0, pps);
        auto anchorTime = xToTime (anchorX);
        pixelsPerSecond = pps;
        viewStart = juce::jmax (0.0, anchorTime - anchorX / pps);
        sendChangeMessage();
    }
};

//==============================================================================
/** Draws a chrome button with an LED colour when toggled. */
void styleToggle (juce::TextButton&, juce::Colour ledColour, const juce::String& tooltip);

//==============================================================================
class TrackHeader : public juce::Component,
                    private juce::Timer
{
public:
    TrackHeader (Session&, te::AudioTrack&, TimelineState&);
    ~TrackHeader() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void refreshInputs();

    te::Track::Ptr trackRef;   // keeps the track alive until this component is rebuilt
    te::AudioTrack& track;

private:
    Session& session;
    TimelineState& state;

    juce::Label nameLabel;
    juce::TextButton armButton { "R" }, monButton { "I" }, muteButton { "M" }, soloButton { "S" }, fxButton { "FX" },
                     takesButton { "T" };
    juce::ComboBox inputBox;
    const bool isBus;
    juce::Rectangle<int> busLabelArea;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Slider pan    { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };

    te::LevelMeasurer::Client meterClient;
    float meterL = -100.0f, meterR = -100.0f;
    juce::StringArray inputNames;   // matches combo ids 2..n

    void timerCallback() override;
    void showFxMenu();
    void showTrackMenu();
    juce::Rectangle<int> getMeterBounds() const;
};

//==============================================================================
class TrackLane : public juce::Component,
                  public juce::FileDragAndDropTarget,
                  private juce::ValueTree::Listener,
                  private juce::AsyncUpdater
{
public:
    TrackLane (Session&, te::AudioTrack&, TimelineState&);
    ~TrackLane() override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    /** Called by the arrange view's timer while recording, to animate the take. */
    void updateRecording();

    te::Track::Ptr trackRef;
    te::AudioTrack& track;

private:
    Session& session;
    TimelineState& state;

    std::map<te::EditItemID, std::unique_ptr<te::SmartThumbnail>> thumbnails;
    te::RecordingThumbnailManager::Thumbnail::Ptr recordingThumb;
    te::TimePosition recordingStart;
    bool wasRecording = false;

    te::Clip::Ptr draggingClip;
    double dragStartClipTime = 0, dragStartMouseTime = 0;
    int dropIndicatorX = -1;

    // Take lanes: swipe to comp
    const bool isTakeLane;
    bool swiping = false;
    double swipeStart = 0, swipeEnd = 0;

    // Fade handles
    enum class FadeDrag { none, fadeIn, fadeOut };
    FadeDrag fadeDrag = FadeDrag::none;
    te::Clip::Ptr fadeClip;
    FadeDrag hitFadeHandle (te::Clip&, juce::Point<float>) const;
    void drawFades (juce::Graphics&, te::AudioClipBase&, juce::Rectangle<float>);

    te::Clip* clipAt (int x) const;
    juce::Rectangle<float> clipBounds (te::Clip&) const;
    double snap (double seconds) const;
    void rebuildThumbnails();
    void showClipMenu (te::Clip*, double time);
    void drawGrid (juce::Graphics&);
    void drawClip (juce::Graphics&, te::Clip&);

    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { triggerAsyncUpdate(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { triggerAsyncUpdate(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
};

//==============================================================================
/** Left-hand header for a take lane: its name, and a menu to use/delete it. */
class TakeLaneHeader : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    TakeLaneHeader (Session&, te::AudioTrack& lane);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    te::Track::Ptr trackRef;
    te::AudioTrack& lane;

private:
    Session& session;
};

//==============================================================================
/** Bars/beats ruler. Click to move the playhead, drag to set the loop range. */
class Ruler : public juce::Component
{
public:
    Ruler (Session&, TimelineState&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Session& session;
    TimelineState& state;
    double dragStart = 0;
    bool draggingLoop = false;
};

//==============================================================================
/** Draws the beat grid for any time-based component. */
void drawBeatGrid (juce::Graphics&, te::Edit&, const TimelineState&, juce::Rectangle<int> area, bool forRuler);

/** Rounds a time to the nearest beat when snapping is on. */
double snapTimeToBeat (te::Edit&, double seconds);
