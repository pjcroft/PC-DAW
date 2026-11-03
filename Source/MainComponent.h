#pragma once

#include <JuceHeader.h>
#include "Session.h"
#include "Theme.h"
#include "TrackComponents.h"

//==============================================================================
/** Chrome button that draws a vector icon instead of text. */
class IconButton : public juce::Button
{
public:
    enum class Icon { rewind, play, stop, record, loop, click, metronome };

    IconButton (const juce::String& name, Icon, juce::Colour ledColour);
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
};

//==============================================================================
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar (Session&);
    void setSession (Session&);

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onRecord;

private:
    Session* session;

    IconButton rewindButton { "Return to Start", IconButton::Icon::rewind, Theme::accent },
               playButton   { "Play",            IconButton::Icon::play,   Theme::solo },
               stopButton   { "Stop",            IconButton::Icon::stop,   Theme::accent },
               recordButton { "Record",          IconButton::Icon::record, Theme::record },
               loopButton   { "Loop",            IconButton::Icon::loop,   Theme::accent },
               clickButton  { "Click",           IconButton::Icon::metronome, Theme::accent };

    juce::Slider bpmSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::ComboBox countInBox;
    juce::Label sessionName;

    juce::Rectangle<int> displayArea, cpuArea;
    juce::String barsText, timeText;
    float cpu = 0.0f;

    void timerCallback() override;
    void bind();
};

//==============================================================================
class PlayheadOverlay : public juce::Component
{
public:
    PlayheadOverlay() { setInterceptsMouseClicks (false, false); }
    void setX (float x)
    {
        if (std::abs (x - playheadX) < 0.5f) return;
        auto old = playheadX;
        playheadX = x;
        repaint (juce::roundToInt (old) - 2, 0, 5, getHeight());
        repaint (juce::roundToInt (x) - 2, 0, 5, getHeight());
    }
    void paint (juce::Graphics& g) override
    {
        if (playheadX < 0 || playheadX > (float) getWidth()) return;
        g.setColour (Theme::accentGlow);
        g.fillRect (playheadX - 2.0f, 0.0f, 5.0f, (float) getHeight());
        g.setColour (Theme::accent);
        g.fillRect (playheadX, 0.0f, 1.0f, (float) getHeight());
    }
    float playheadX = -10;
};

//==============================================================================
/** Track headers on the left, timeline lanes on the right, ruler on top. */
class ArrangeView : public juce::Component,
                    private juce::ChangeListener,
                    private juce::ScrollBar::Listener,
                    private juce::ValueTree::Listener,
                    private juce::AsyncUpdater,
                    private juce::Timer
{
public:
    static constexpr int headerWidth = 300, trackHeight = 84, rulerHeight = 26;

    ArrangeView (Session&, TimelineState&);
    ~ArrangeView() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void rebuildTracks();
    void refreshInputs();
    void zoomBy (double factor);

private:
    struct TrackList : public juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (Theme::bgDeep); }
    };

    Session& session;
    TimelineState& state;

    Ruler ruler;
    juce::Viewport viewport;
    TrackList trackList;
    juce::ScrollBar hScroll { false };
    PlayheadOverlay playhead;

    juce::OwnedArray<TrackHeader> headers;
    juce::OwnedArray<TrackLane> lanes;

    void layoutTracks();
    void updateScrollBar();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void timerCallback() override;

    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree& c) override         { if (te::TrackList::isTrack (c)) triggerAsyncUpdate(); juce::ignoreUnused (p); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree& c, int) override  { if (te::TrackList::isTrack (c)) triggerAsyncUpdate(); juce::ignoreUnused (p); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override               { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override                                                   { rebuildTracks(); }
};

//==============================================================================
class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::ChangeListener
{
public:
    MainComponent (te::Engine&, const juce::File& initialSession = {});
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override;

    /** Saves the session; returns false if the user cancelled. */
    bool saveBeforeQuit();

    /** Writes a PNG of the window (used for --screenshot). */
    void saveScreenshot (const juce::File&);

private:
    enum MenuIDs
    {
        cmd_newSession = 1, cmd_openSession, cmd_saveSession, cmd_saveSessionAs, cmd_exportStems, cmd_revealSession,
        cmd_undo, cmd_redo, cmd_deleteSelection, cmd_splitAtPlayhead, cmd_toggleSnap,
        cmd_addTrack, cmd_addInstrumentTrack, cmd_deleteTrack, cmd_importFile,
        cmd_play, cmd_record, cmd_returnToStart, cmd_toggleLoop, cmd_toggleClick,
        cmd_audioSettings, cmd_pluginManager,
        cmd_zoomIn, cmd_zoomOut,
        cmd_recentBase = 1000
    };

    te::Engine& engine;
    std::unique_ptr<Session> session;
    TimelineState timeline;
    MidnightChromeLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    std::unique_ptr<TransportBar> transport;
    std::unique_ptr<ArrangeView> arrange;
    juce::Label statusBar;

    juce::ApplicationProperties appProperties;

    void openSession (const juce::File&);
    void newSessionDialog();
    void openSessionDialog();
    void saveAsDialog();
    void exportStemsDialog();
    void showAudioSettings();
    void showPluginManager();
    void addInstrumentTrackMenu();
    void importFileDialog();
    void deleteSelectedClip();
    void splitSelectedAtPlayhead();
    void toggleRecord();
    void setStatus (const juce::String&);

    te::AudioTrack* getSelectedTrack() const;
    juce::StringArray getRecentSessions();
    void addToRecent (const juce::File&);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
