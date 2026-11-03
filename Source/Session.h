#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
/**
    Thin layer over a te::Edit with the operations the UI needs:
    tracks, input routing, arming, plugins, import and stem export.

    Sessions live in their own folder: <Music>/Croft DAW/<Name>/<Name>.tracktionedit
    Recorded takes are written next to the session file.
*/
class Session : private te::TransportControl::Listener
{
public:
    static constexpr int defaultNumTracks = 8;
    static constexpr int maxTracks        = 64;

    /** Opens the session file if it exists, otherwise creates a new empty one. */
    Session (te::Engine&, const juce::File& editFile);
    ~Session();

    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;

    juce::File getFile() const;
    juce::String getName() const              { return getFile().getFileNameWithoutExtension(); }
    void save();

    static juce::File getSessionsRoot();
    static juce::File makeNewSessionFile (const juce::String& name);

    //==============================================================================
    // Engine-level setup, call once at startup and whenever the audio device changes.
    static void configureInputDevices (te::Engine&);

    //==============================================================================
    // Tracks
    /** Every audio track in the edit, including buses and take lanes. */
    juce::Array<te::AudioTrack*> getAllAudioTracks() const  { return te::getAudioTracks (*edit); }
    /** Tracks shown as rows: record/instrument tracks and buses (not take lanes). */
    juce::Array<te::AudioTrack*> getTracks() const;
    te::AudioTrack* addAudioTrack();
    te::AudioTrack* addInstrumentTrack (const juce::PluginDescription&);
    void deleteTrack (te::AudioTrack&);

    //==============================================================================
    // Inputs (one physical input per track; assigning an input moves it off other tracks)
    juce::Array<te::InputDeviceInstance*> getAvailableInputs() const;
    te::InputDeviceInstance* getInputFor (te::AudioTrack&) const;
    void setInputFor (te::AudioTrack&, te::InputDeviceInstance* /*nullptr = none*/);

    bool isArmed (te::AudioTrack&) const;
    void setArmed (te::AudioTrack&, bool);

    /** Input monitoring for the track's input: automatic = hear input while armed. */
    te::InputDevice::MonitorMode getMonitorMode (te::AudioTrack&) const;
    void setMonitorMode (te::AudioTrack&, te::InputDevice::MonitorMode);

    //==============================================================================
    // Plugins (user inserts sit before the track's volume/pan plugin)
    te::Plugin::Array getInsertPlugins (te::AudioTrack&) const;
    te::Plugin::Ptr addPlugin (te::AudioTrack&, const juce::PluginDescription&);

    //==============================================================================
    // Buses
    //  A bus is an audio track flagged as a bus. It receives audio two ways:
    //   - aux sends (an AuxSend plugin on a track -> the bus's AuxReturn plugin)
    //   - output routing (a track's output set to the bus, i.e. a submix/group)
    static constexpr int maxBuses = 16;

    juce::Array<te::AudioTrack*> getBuses() const;
    juce::Array<te::AudioTrack*> getRecordTracks() const;      // non-bus tracks
    static bool isBus (const te::AudioTrack&);
    static int getBusNumber (const te::AudioTrack&);           // aux bus index, -1 if not a bus
    te::AudioTrack* getBusForNumber (int busNumber) const;
    te::AudioTrack* addBus (const juce::String& name = {});

    /** Post-fader send from a track to a bus. Returns the existing send if there is one. */
    te::AuxSendPlugin* getSend (te::AudioTrack&, int busNumber) const;
    te::AuxSendPlugin* addSend (te::AudioTrack&, int busNumber);
    void removeSend (te::AudioTrack&, int busNumber);

    /** Where a track's audio goes: nullptr = master. */
    te::AudioTrack* getOutputBus (te::AudioTrack&) const;
    void setOutputBus (te::AudioTrack&, te::AudioTrack* bus);

    //==============================================================================
    // Take lanes & comping
    //  Recording over existing audio (or loop-recording several passes) puts every pass on its own
    //  "take lane" under the track. The track itself holds the comp: swipe across a take lane to
    //  copy that part of the take onto the track.
    static bool isTakeLane (const te::AudioTrack&);
    te::AudioTrack* getTakeLaneParent (const te::AudioTrack&) const;
    juce::Array<te::AudioTrack*> getTakeLanes (const te::AudioTrack& parent) const;
    te::AudioTrack* addTakeLane (te::AudioTrack& parent);
    void deleteTakeLanes (te::AudioTrack& parent);

    static bool areTakesShown (const te::AudioTrack&);
    static void setTakesShown (te::AudioTrack&, bool);

    /** Copies [start, end) of a take lane onto its parent track, replacing what was there. */
    void compFromLane (te::AudioTrack& lane, double startSeconds, double endSeconds);

    /** Which take lane a comp segment on the parent came from (invalid if none). */
    static te::EditItemID getCompSource (const te::Clip&);

    /** Removes [start, end) from a track, splitting clips at the edges. */
    static void clearRange (te::ClipTrack&, double startSeconds, double endSeconds);

    /** Called after a recording pass finishes on a track. */
    void handleRecordedClips (te::AudioTrack&, const juce::ReferenceCountedArray<te::Clip>&);

    //==============================================================================
    // Fades
    /** Makes overlapping clips on a track crossfade automatically. */
    static void enableAutoCrossfades (te::AudioTrack&);

    //==============================================================================
    // Clips
    /** Drops a .wav/.aif/.flac/.mid file on a track at a given time. */
    bool importFile (te::AudioTrack&, const juce::File&, te::TimePosition);

    //==============================================================================
    // Transport helpers
    double getBpm() const;
    void setBpm (double);

    /** Renders each non-empty track to <folder>/<NN> <Track Name>.wav, all starting at 0:00
        so they line up when dragged into another DAW. Returns the files written. */
    juce::Array<juce::File> exportStems (const juce::File& folder, int bitDepth = 24);

private:
    void recordingFinished (te::InputDeviceInstance&, te::EditItemID, const juce::ReferenceCountedArray<te::Clip>&) override;

    JUCE_DECLARE_NON_COPYABLE (Session)
};
