#include "Session.h"

//==============================================================================
Session::Session (te::Engine& e, const juce::File& editFile)
    : engine (e)
{
    editFile.getParentDirectory().createDirectory();

    // Load or create the Edit ourselves so we can supply a file-path resolver that understands
    // both ways Tracktion stores relative paths (see below).
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

            // Tracktion writes relative paths in two styles: relative to the session folder
            // ("take.wav", used for recordings) and relative to the session *file* as if it were
            // a folder ("../take.wav", used for imports). Accept both.
            auto fromFile   = editFile.getChildFile (path);
            auto fromFolder = editFile.getSiblingFile (path);

            if (fromFolder.exists()) return fromFolder;
            if (fromFile.exists())   return fromFile;

            return path.startsWith ("..") ? fromFile : fromFolder;
        },
        0
    });

    edit->playInStopEnabled = true;   // hear armed inputs while stopped
    edit->getTransport().ensureContextAllocated();
    edit->getTransport().addListener (this);

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
        edit->getTransport().removeListener (this);
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
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Croft DAW");
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
juce::Array<te::AudioTrack*> Session::getTracks() const
{
    juce::Array<te::AudioTrack*> result;

    for (auto t : getAllAudioTracks())
        if (! isTakeLane (*t))
            result.add (t);

    // Keep buses after the record tracks, like an aux section
    std::stable_sort (result.begin(), result.end(), [] (te::AudioTrack* a, te::AudioTrack* b)
                      { return ! isBus (*a) && isBus (*b); });
    return result;
}

juce::Array<te::AudioTrack*> Session::getRecordTracks() const
{
    juce::Array<te::AudioTrack*> result;

    for (auto t : getAllAudioTracks())
        if (! isTakeLane (*t) && ! isBus (*t))
            result.add (t);

    return result;
}

te::AudioTrack* Session::addAudioTrack()
{
    auto tracks = getRecordTracks();

    if (tracks.size() >= maxTracks)
        return nullptr;

    // Insert after the last record track so buses stay at the end
    te::Track* preceding = tracks.isEmpty() ? nullptr : tracks.getLast();

    for (auto lane : (tracks.isEmpty() ? juce::Array<te::AudioTrack*>() : getTakeLanes (*tracks.getLast())))
        preceding = lane;

    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, preceding), nullptr);

    if (t == nullptr)
        return nullptr;

    t->setName ("Track " + juce::String (tracks.size() + 1));
    return t.get();
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
        if (p != t.getVolumePlugin() && p != t.getLevelMeterPlugin()
             && dynamic_cast<te::AuxSendPlugin*> (p) == nullptr
             && dynamic_cast<te::AuxReturnPlugin*> (p) == nullptr)
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
    auto clip = t.insertWaveClip (file.getFileNameWithoutExtension(), file, { { time, length }, {} }, false);

    if (clip != nullptr)
        clip->setAutoCrossfade (true);

    return clip != nullptr;
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

    for (auto t : getRecordTracks())
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

//==============================================================================
//  Buses, sends, output routing
//==============================================================================
static const juce::Identifier busNumberID ("croftBusNumber"),
                              takeLaneOfID ("croftTakeLaneOf"),
                              showTakesID ("croftShowTakes"),
                              compSourceID ("croftCompSource");

bool Session::isBus (const te::AudioTrack& t)
{
    return getBusNumber (t) >= 0;
}

int Session::getBusNumber (const te::AudioTrack& t)
{
    return (int) t.state.getProperty (busNumberID, -1);
}

juce::Array<te::AudioTrack*> Session::getBuses() const
{
    juce::Array<te::AudioTrack*> result;

    for (auto t : getAllAudioTracks())
        if (isBus (*t))
            result.add (t);

    return result;
}

te::AudioTrack* Session::getBusForNumber (int busNumber) const
{
    for (auto t : getBuses())
        if (getBusNumber (*t) == busNumber)
            return t;

    return nullptr;
}

te::AudioTrack* Session::addBus (const juce::String& name)
{
    int number = -1;

    for (int i = 0; i < maxBuses; ++i)
    {
        if (getBusForNumber (i) == nullptr)
        {
            number = i;
            break;
        }
    }

    if (number < 0)
        return nullptr;

    auto all = getAllAudioTracks();
    auto t = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, all.getLast()), nullptr);

    if (t == nullptr)
        return nullptr;

    auto busName = name.isNotEmpty() ? name : juce::String (number == 0 ? "Reverb" : number == 1 ? "Delay" : "Bus " + juce::String (number + 1));
    t->setName (busName);
    t->state.setProperty (busNumberID, number, &edit->getUndoManager());
    t->setColour (juce::Colour (0xff8a93a6));
    edit->setAuxBusName (number, busName);

    // Receives the aux sends for this bus number
    if (auto ret = dynamic_cast<te::AuxReturnPlugin*> (edit->getPluginCache().createNewPlugin (te::AuxReturnPlugin::xmlTypeName, {}).get()))
    {
        ret->busNumber = number;
        t->pluginList.insertPlugin (*ret, 0, nullptr);
    }

    return t.get();
}

te::AuxSendPlugin* Session::getSend (te::AudioTrack& t, int busNumber) const
{
    for (auto p : t.pluginList.getPlugins())
        if (auto send = dynamic_cast<te::AuxSendPlugin*> (p))
            if (send->getBusNumber() == busNumber)
                return send;

    return nullptr;
}

te::AuxSendPlugin* Session::addSend (te::AudioTrack& t, int busNumber)
{
    if (auto existing = getSend (t, busNumber))
        return existing;

    if (getBusNumber (t) == busNumber)
        return nullptr;   // a bus can't send to itself

    auto plugin = edit->getPluginCache().createNewPlugin (te::AuxSendPlugin::xmlTypeName, {});
    auto send = dynamic_cast<te::AuxSendPlugin*> (plugin.get());

    if (send == nullptr)
        return nullptr;

    send->busNumber = busNumber;
    send->setGainDb (-10.0f);

    // Post-fader: straight after the volume/pan plugin
    int index = t.pluginList.size();

    if (auto vol = t.getVolumePlugin())
        index = t.pluginList.indexOf (vol) + 1;

    t.pluginList.insertPlugin (plugin, index, nullptr);
    return send;
}

void Session::removeSend (te::AudioTrack& t, int busNumber)
{
    if (auto send = getSend (t, busNumber))
        send->deleteFromParent();
}

te::AudioTrack* Session::getOutputBus (te::AudioTrack& t) const
{
    return t.getOutput().getDestinationTrack();
}

void Session::setOutputBus (te::AudioTrack& t, te::AudioTrack* bus)
{
    if (bus == &t)
        return;

    if (bus != nullptr)
        t.getOutput().setOutputToTrack (bus);
    else
        t.getOutput().setOutputToDefaultDevice (false);
}

//==============================================================================
//  Take lanes & comping
//==============================================================================
bool Session::isTakeLane (const te::AudioTrack& t)
{
    return t.state.hasProperty (takeLaneOfID);
}

te::AudioTrack* Session::getTakeLaneParent (const te::AudioTrack& lane) const
{
    if (! isTakeLane (lane))
        return nullptr;

    return te::findAudioTrackForID (*edit, te::EditItemID::fromVar (lane.state.getProperty (takeLaneOfID)));
}

juce::Array<te::AudioTrack*> Session::getTakeLanes (const te::AudioTrack& parent) const
{
    juce::Array<te::AudioTrack*> result;
    auto parentID = parent.itemID.toVar();

    for (auto t : getAllAudioTracks())
        if (isTakeLane (*t) && t->state.getProperty (takeLaneOfID) == parentID)
            result.add (t);

    return result;
}

te::AudioTrack* Session::addTakeLane (te::AudioTrack& parent)
{
    auto lanes = getTakeLanes (parent);
    te::Track* preceding = lanes.isEmpty() ? static_cast<te::Track*> (&parent) : lanes.getLast();

    auto lane = edit->insertNewAudioTrack (te::TrackInsertPoint (nullptr, preceding), nullptr);

    if (lane == nullptr)
        return nullptr;

    lane->state.setProperty (takeLaneOfID, parent.itemID.toVar(), &edit->getUndoManager());
    lane->setName ("Take " + juce::String (lanes.size() + 1));
    lane->setMute (true);   // lanes are for picking; the parent track plays the comp
    return lane.get();
}

void Session::deleteTakeLanes (te::AudioTrack& parent)
{
    for (auto lane : getTakeLanes (parent))
        edit->deleteTrack (lane);

    for (auto c : parent.getClips())
        c->state.removeProperty (compSourceID, &edit->getUndoManager());
}

bool Session::areTakesShown (const te::AudioTrack& t)        { return t.state.getProperty (showTakesID, true); }
void Session::setTakesShown (te::AudioTrack& t, bool shown)  { t.state.setProperty (showTakesID, shown, nullptr); }

te::EditItemID Session::getCompSource (const te::Clip& c)
{
    if (! c.state.hasProperty (compSourceID))
        return {};

    return te::EditItemID::fromVar (c.state.getProperty (compSourceID));
}

void Session::clearRange (te::ClipTrack& track, double start, double end)
{
    if (end <= start)
        return;

    const auto a = te::TimePosition::fromSeconds (start);
    const auto b = te::TimePosition::fromSeconds (end);

    for (auto c : track.getClips())
    {
        te::Clip::Ptr clip (c);
        auto pos = clip->getPosition();

        if (pos.getEnd() <= a || pos.getStart() >= b)
            continue;

        // Cut off whatever sticks out past the end, then past the start
        if (pos.getEnd() > b && pos.getStart() < b)
            track.splitClip (*clip, b);   // clip keeps the left part

        pos = clip->getPosition();

        if (pos.getStart() < a)
        {
            if (auto right = track.splitClip (*clip, a))
                right->removeFromParent();
        }
        else
        {
            clip->removeFromParent();
        }
    }

    // Don't leave tiny slivers (a few ms) next to the cleared range
    for (auto c : track.getClips())
    {
        auto pos = c->getPosition();

        if (pos.getLength().inSeconds() < 0.02 && (pos.time.overlaps ({ a - te::TimeDuration::fromSeconds (0.02), b + te::TimeDuration::fromSeconds (0.02) })))
            c->removeFromParent();
    }
}

static te::WaveAudioClip* copyWaveSegment (te::AudioTrack& dest, te::WaveAudioClip& src, te::TimeRange range)
{
    auto pos = src.getPosition();
    auto overlap = range.getIntersectionWith (pos.time);

    if (overlap.isEmpty())
        return nullptr;

    auto offset = pos.getOffset() + (overlap.getStart() - pos.getStart());
    auto file = src.getAudioFile().getFile();
    auto clip = dest.insertWaveClip (src.getName(), file, { overlap, offset }, false);

    if (clip != nullptr)
    {
        clip->setGainDB (src.getGainDB());
        clip->setAutoCrossfade (true);
    }

    return clip.get();
}

void Session::compFromLane (te::AudioTrack& lane, double start, double end)
{
    auto parent = getTakeLaneParent (lane);

    if (parent == nullptr || end <= start)
        return;

    auto range = te::TimeRange (te::TimePosition::fromSeconds (start), te::TimePosition::fromSeconds (end));
    clearRange (*parent, start, end);

    for (auto c : lane.getClips())
    {
        if (auto wac = dynamic_cast<te::WaveAudioClip*> (c))
        {
            if (auto piece = copyWaveSegment (*parent, *wac, range))
            {
                piece->state.setProperty (compSourceID, lane.itemID.toVar(), &edit->getUndoManager());

                // Short fades at the comp boundaries so edits never click
                const auto edge = te::TimeDuration::fromSeconds (0.008);
                if (piece->getPosition().getStart() > wac->getPosition().getStart()) piece->setFadeIn (edge);
                if (piece->getPosition().getEnd() < wac->getPosition().getEnd())     piece->setFadeOut (edge);
            }
        }
    }
}

static juce::Array<juce::File> getTakeFiles (te::WaveAudioClip& clip)
{
    juce::Array<juce::File> files;
    auto takes = clip.state.getChildWithName (te::IDs::TAKES);

    for (auto take : takes)
    {
        if (take.getProperty (te::IDs::isComp, false))
            continue;

        te::SourceFileReference ref (clip.edit, take, te::IDs::source);
        auto f = ref.getFile();

        if (juce::SystemStats::getEnvironmentVariable ("CROFT_DEBUG", {}).isNotEmpty())
            std::cout << "[session] take source='" << take.getProperty (te::IDs::source).toString() << "' -> " << f.getFullPathName() << " exists=" << f.existsAsFile() << std::endl;

        if (f.existsAsFile())
            files.addIfNotAlreadyThere (f);
    }

    if (files.isEmpty())
        files.add (clip.getAudioFile().getFile());

    return files;
}

void Session::recordingFinished (te::InputDeviceInstance&, te::EditItemID targetID,
                                 const juce::ReferenceCountedArray<te::Clip>& clips)
{
    DBG ("recordingFinished: " << clips.size() << " clip(s)");
   #if JUCE_DEBUG == 0
    if (juce::SystemStats::getEnvironmentVariable ("CROFT_DEBUG", {}).isNotEmpty())
        std::cout << "[session] recordingFinished: " << clips.size() << " clip(s) on " << targetID.toString() << std::endl;
   #endif

    if (auto t = te::findAudioTrackForID (*edit, targetID))
        handleRecordedClips (*t, clips);
}

void Session::handleRecordedClips (te::AudioTrack& track, const juce::ReferenceCountedArray<te::Clip>& recorded)
{
    if (isTakeLane (track) || isBus (track))
        return;

    for (auto c : recorded)
    {
        auto wac = dynamic_cast<te::WaveAudioClip*> (c);

        if (wac == nullptr)
            continue;

        wac->setAutoCrossfade (true);

        const auto range = wac->getPosition().time;
        const auto takeFiles = getTakeFiles (*wac);

        // Older clips this pass recorded over
        juce::ReferenceCountedArray<te::Clip> overlapped;

        for (auto other : track.getClips())
            if (! recorded.contains (other) && other->getPosition().time.overlaps (range))
                overlapped.add (other);

        if (juce::SystemStats::getEnvironmentVariable ("CROFT_DEBUG", {}).isNotEmpty())
            std::cout << "[session] recorded clip " << range.getStart().inSeconds() << "-" << range.getEnd().inSeconds()
                      << " takes-tree=" << wac->state.getChildWithName (te::IDs::TAKES).getNumChildren()
                      << " takeFiles=" << takeFiles.size() << " overlapped=" << overlapped.size()
                      << " trackClips=" << track.getClips().size() << std::endl;

        if (takeFiles.size() < 2 && overlapped.isEmpty())
            continue;   // a plain first pass: nothing to comp

        // First time we comp on this track: what was there becomes "Take 1"
        if (getTakeLanes (track).isEmpty() && ! overlapped.isEmpty())
        {
            if (auto lane = addTakeLane (track))
                for (auto old : overlapped)
                    if (auto oldWave = dynamic_cast<te::WaveAudioClip*> (old))
                        copyWaveSegment (*lane, *oldWave, oldWave->getPosition().time);
        }

        // Every pass of this recording gets its own lane. The track plays the newest *complete*
        // pass (stopping half-way through a loop leaves a short final take).
        te::AudioTrack* lastLane = nullptr;
        const auto neededLength = range.getLength().inSeconds() * 0.95;

        for (auto& f : takeFiles)
        {
            if (auto lane = addTakeLane (track))
            {
                if (auto clip = lane->insertWaveClip (f.getFileNameWithoutExtension(), f, { range, wac->getPosition().getOffset() }, false))
                    clip->setAutoCrossfade (true);

                if (lastLane == nullptr || te::AudioFile (engine, f).getLength() >= neededLength)
                    lastLane = lane;
            }
        }

        // The track plays the newest pass across the recorded range
        auto start = range.getStart().inSeconds(), end = range.getEnd().inSeconds();
        te::Clip::Ptr keep (wac);
        wac->removeFromParent();
        clearRange (track, start, end);

        if (lastLane != nullptr)
            compFromLane (*lastLane, start, end);

        setTakesShown (track, true);
    }
}

void Session::enableAutoCrossfades (te::AudioTrack& t)
{
    for (auto c : t.getClips())
        if (auto ac = dynamic_cast<te::AudioClipBase*> (c))
            ac->setAutoCrossfade (true);
}
