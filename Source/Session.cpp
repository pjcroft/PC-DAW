#include "Session.h"

//==============================================================================
Session::Session (te::Engine& e, const juce::File& editFile)
    : engine (e)
{
    editFile.getParentDirectory().createDirectory();

    // Load or create the Edit ourselves so we can supply a file-path resolver.
    // Tracktion writes relative clip paths with File::getRelativePathFrom (editFile), i.e. as if the
    // .tracktionedit file were a folder ("../take.wav"); resolve them the same way so takes stored
    // next to the session file are found again.
    auto state = te::loadEditFromFile (engine, editFile.existsAsFile() ? editFile : juce::File(),
                                       te::ProjectItemID::createNewID (te::ProjectID{}));
    auto id = te::ProjectItemID::fromProperty (state, te::IDs::projectID);

    if (! id.isValid())
        id = te::ProjectItemID::createNewID (te::ProjectID{});

    edit = te::Edit::createEdit (te::Edit::Options
    {
        engine, state, id, te::Edit::forEditing, nullptr, te::Edit::getDefaultNumUndoLevels(),
        [editFile] { return editFile; },
        [editFile] (const juce::String& path) -> juce::File
        {
            if (juce::File::isAbsolutePath (path))
                return juce::File (path);

            return editFile.getChildFile (path);
        },
        0
    });

    edit->playInStopEnabled = true;   // hear armed inputs while stopped
    edit->getTransport().ensureContextAllocated();

    if (getTracks().isEmpty())
    {
        edit->ensureNumberOfAudioTracks (defaultNumTracks);

        int i = 0;
        for (auto t : getTracks())
            t->setName ("Track " + juce::String (++i));

        // Default routing: interface input N -> track N
        auto inputs = getAvailableInputs();
        int trackIndex = 0;

        for (auto in : inputs)
        {
            if (in->getInputDevice().getDeviceType() != te::InputDevice::waveDevice)
                continue;

            if (auto t = getTracks()[trackIndex++])
                setInputFor (*t, in);
            else
                break;
        }

        edit->getUndoManager().clearUndoHistory();
    }

    edit->restartPlayback();
}

Session::~Session()
{
    if (edit != nullptr)
    {
        edit->getTransport().stop (false, true);
        save();
    }
}

juce::File Session::getFile() const
{
    return edit->editFileRetriever();
}

void Session::save()
{
    te::EditFileOperations (*edit).save (true, true, false);
}

juce::File Session::getSessionsRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Tapedeck");
}

juce::File Session::makeNewSessionFile (const juce::String& name)
{
    auto safe = juce::File::createLegalFileName (name.trim().isEmpty() ? juce::String ("Untitled") : name.trim());
    auto folder = getSessionsRoot().getNonexistentChildFile (safe, {}, false);
    return folder.getChildFile (folder.getFileName() + te::editFileSuffix);
}

//==============================================================================
void Session::configureInputDevices (te::Engine& engine)
{
    auto& dm = engine.getDeviceManager();

    // Treat every interface channel as its own mono input (guitar DI, mic, etc.).
    // This rebuilds the device list asynchronously, so it's only done when needed
    // (this function is called again from the device-change callback).
    bool needsMono = false;

    for (auto w : dm.getWaveInputDevices())
        if (w->getChannels().getNumChannels() != 1)
            needsMono = true;

    if (needsMono)
    {
        dm.setAllWaveInputsToNumChannels (1);
        return;
    }

    for (auto w : dm.getWaveInputDevices())
    {
        if (! w->isEnabled())
            dm.setDeviceEnabled (*w, true);

        // Default: hear the input (through the track's plugins) while the track is armed.
        if (w->getMonitorMode() == te::InputDevice::MonitorMode::off)
            w->setMonitorMode (te::InputDevice::MonitorMode::automatic);
    }

    for (auto& m : dm.getMidiInDevices())
    {
        if (! m->isEnabled())
            m->setEnabled (true);

        if (m->getMonitorMode() == te::InputDevice::MonitorMode::off)
            m->setMonitorMode (te::InputDevice::MonitorMode::automatic);
    }
}

//==============================================================================
te::AudioTrack* Session::addAudioTrack()
{
    auto tracks = getTracks();

    if (tracks.size() >= maxTracks)
        return nullptr;

    edit->ensureNumberOfAudioTracks (tracks.size() + 1);
    auto t = getTracks().getLast();
    t->setName ("Track " + juce::String (tracks.size() + 1));
    return t;
}

te::AudioTrack* Session::addInstrumentTrack (const juce::PluginDescription& desc)
{
    auto t = addAudioTrack();

    if (t == nullptr)
        return nullptr;

    t->setName (desc.name);
    addPlugin (*t, desc);

    // Route the first MIDI keyboard/pad (if any) to it for live playing
    for (auto in : getAvailableInputs())
    {
        if (in->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice)
        {
            setInputFor (*t, in);
            break;
        }
    }

    return t;
}

void Session::deleteTrack (te::AudioTrack& t)
{
    edit->deleteTrack (&t);
}

//==============================================================================
juce::Array<te::InputDeviceInstance*> Session::getAvailableInputs() const
{
    juce::Array<te::InputDeviceInstance*> result;

    for (auto in : edit->getAllInputDevices())
    {
        auto type = in->getInputDevice().getDeviceType();

        if (type == te::InputDevice::waveDevice || type == te::InputDevice::physicalMidiDevice)
            result.add (in);
    }

    return result;
}

te::InputDeviceInstance* Session::getInputFor (te::AudioTrack& t) const
{
    for (auto in : edit->getAllInputDevices())
        if (in->getTargets().contains (t.itemID))
            return in;

    return nullptr;
}

void Session::setInputFor (te::AudioTrack& t, te::InputDeviceInstance* newInput)
{
    auto* um = &edit->getUndoManager();

    for (auto in : edit->getAllInputDevices())
        if (in != newInput && in->getTargets().contains (t.itemID))
            [[maybe_unused]] auto r = in->removeTarget (t.itemID, um);

    if (newInput != nullptr && ! newInput->getTargets().contains (t.itemID))
    {
        // An input feeds one track at a time: free it from wherever it was.
        for (auto id : newInput->getTargets())
            [[maybe_unused]] auto r = newInput->removeTarget (id, um);

        [[maybe_unused]] auto r = newInput->setTarget (t.itemID, true, um, 0);
    }

    edit->restartPlayback();
}

bool Session::isArmed (te::AudioTrack& t) const
{
    if (auto in = getInputFor (t))
        return in->isRecordingEnabled (t.itemID);

    return false;
}

void Session::setArmed (te::AudioTrack& t, bool arm)
{
    if (auto in = getInputFor (t))
        in->setRecordingEnabled (t.itemID, arm);
}

te::InputDevice::MonitorMode Session::getMonitorMode (te::AudioTrack& t) const
{
    if (auto in = getInputFor (t))
        return in->getInputDevice().getMonitorMode();

    return te::InputDevice::MonitorMode::off;
}

void Session::setMonitorMode (te::AudioTrack& t, te::InputDevice::MonitorMode mode)
{
    if (auto in = getInputFor (t))
        in->getInputDevice().setMonitorMode (mode);
}

//==============================================================================
te::Plugin::Array Session::getInsertPlugins (te::AudioTrack& t) const
{
    te::Plugin::Array result;

    for (auto p : t.pluginList.getPlugins())
        if (p != t.getVolumePlugin() && p != t.getLevelMeterPlugin())
            result.add (p);

    return result;
}

te::Plugin::Ptr Session::addPlugin (te::AudioTrack& t, const juce::PluginDescription& desc)
{
    auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

    if (plugin == nullptr)
        return {};

    // Insert just before the volume/pan stage so the fader is post-FX.
    int index = t.pluginList.size();

    if (auto vol = t.getVolumePlugin())
        index = t.pluginList.indexOf (vol);

    t.pluginList.insertPlugin (plugin, index, nullptr);
    return plugin;
}

//==============================================================================
bool Session::importFile (te::AudioTrack& t, const juce::File& file, te::TimePosition time)
{
    if (file.hasFileExtension ("mid;midi"))
    {
        if (auto clip = te::createClipFromFile (file, t, false))
        {
            clip->setStart (time, false, true);
            clip->setName (file.getFileNameWithoutExtension());
            return true;
        }

        return false;
    }

    te::AudioFile audioFile (engine, file);

    if (! audioFile.isValid())
        return false;

    auto length = te::TimeDuration::fromSeconds (audioFile.getLength());
    return t.insertWaveClip (file.getFileNameWithoutExtension(), file,
                             { { time, length }, {} }, false) != nullptr;
}

//==============================================================================
double Session::getBpm() const
{
    if (auto tempo = edit->tempoSequence.getTempo (0))
        return tempo->getBpm();

    return 120.0;
}

void Session::setBpm (double bpm)
{
    if (auto tempo = edit->tempoSequence.getTempo (0))
        tempo->setBpm (juce::jlimit (20.0, 300.0, bpm));
}

//==============================================================================
juce::Array<juce::File> Session::exportStems (const juce::File& folder, int bitDepth)
{
    juce::Array<juce::File> written;
    folder.createDirectory();

    edit->getTransport().stop (false, true);

    const auto end = te::toPosition (edit->getLength());

    if (end <= te::TimePosition())
        return written;

    int index = 0;

    for (auto t : getTracks())
    {
        ++index;

        if (t->getClips().isEmpty())
            continue;

        // A MIDI-only track with no instrument plugin would just render silence.
        bool hasAudioClips = false, hasInstrument = false;

        for (auto c : t->getClips())
            if (dynamic_cast<te::WaveAudioClip*> (c) != nullptr)
                hasAudioClips = true;

        for (auto p : getInsertPlugins (*t))
            if (p->isSynth())
                hasInstrument = true;

        if (! hasAudioClips && ! hasInstrument)
            continue;

        auto name = juce::String (index).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName());
        auto file = folder.getChildFile (name + ".wav");

        te::Renderer::Parameters params (*edit);
        params.destFile             = file;
        params.audioFormat          = engine.getAudioFileFormatManager().getWavFormat();
        params.bitDepth             = bitDepth;
        params.sampleRateForAudio   = engine.getDeviceManager().getSampleRate();
        params.blockSizeForAudio    = 512;
        params.time                 = { te::TimePosition(), end };   // always from 0 so stems line up
        params.endAllowance         = te::TimeDuration::fromSeconds (2.0);
        params.tracksToDo           = te::toBitSet (juce::Array<te::Track*> { t });
        params.usePlugins           = true;
        params.useMasterPlugins     = false;
        params.canRenderInMono      = false;
        params.checkNodesForAudio   = false;

        auto result = te::Renderer::renderToFile ("Exporting " + t->getName(), params);

        if (result.existsAsFile())
            written.add (result);
    }

    return written;
}
