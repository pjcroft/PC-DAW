#pragma once

#include <JuceHeader.h>

namespace te = tracktion;

//==============================================================================
/**
    Thin layer over a te::Edit with the operations the UI needs:
    tracks, input routing, arming, plugins, import and stem export.

    Sessions live in their own folder: <Music>/Tapedeck/<Name>/<Name>.tracktionedit
    Recorded takes are written next to the session file.
*/
class Session
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
    juce::Array<te::AudioTrack*> getTracks() const    { return te::getAudioTracks (*edit); }
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
    JUCE_DECLARE_NON_COPYABLE (Session)
};
