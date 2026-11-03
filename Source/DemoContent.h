#pragma once

#include <JuceHeader.h>
#include "Session.h"

//==============================================================================
/** Generates a throwaway session with synthesized guitar/bass takes and a MIDI
    drum groove. Used for screenshots and for trying the app without an interface. */
namespace DemoContent
{
    inline void writeWav (const juce::File& f, const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        f.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (f.createOutputStream().release());

        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), sampleRate,
                                                                    (unsigned int) buffer.getNumChannels(), 24, {}, 0)))
        {
            out.release();
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }
    }

    /** Karplus-Strong plucked string. */
    inline void pluck (juce::AudioBuffer<float>& buf, int start, double freq, double sr, float gain, float decay, int length, juce::Random& rng)
    {
        const int period = juce::jmax (2, (int) (sr / freq));
        std::vector<float> line ((size_t) period);
        for (auto& s : line) s = rng.nextFloat() * 2.0f - 1.0f;

        auto* d = buf.getWritePointer (0);
        size_t idx = 0;

        for (int i = 0; i < length && start + i < buf.getNumSamples(); ++i)
        {
            auto next = (idx + 1) % (size_t) period;
            auto v = line[idx];
            line[idx] = decay * 0.5f * (line[idx] + line[next]);
            idx = next;
            d[start + i] += std::tanh (v * 3.0f) * gain;   // a bit of drive
        }
    }

    inline juce::File prepareDemoSession (te::Engine& engine)
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TapedeckDemo");
        folder.deleteRecursively();
        folder.createDirectory();

        const double sr = 44100.0, bpm = 120.0, beat = 60.0 / bpm;
        const int bars = 8;
        const int total = (int) (sr * beat * 4 * bars);
        juce::Random rng (42);

        auto midiToHz = [] (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); };

        // Rhythm guitar: palm-muted eighths + chord stabs, E5 / C5 / D5 / A5
        const int roots[] = { 40, 36, 38, 45 };
        auto makeRhythm = [&] (unsigned seed)
        {
            juce::Random r ((juce::int64) seed);
            juce::AudioBuffer<float> b (1, total);
            b.clear();

            for (int bar = 0; bar < bars; ++bar)
            {
                int root = roots[(bar / 2) % 4];
                for (int e = 0; e < 8; ++e)
                {
                    int start = (int) (((bar * 4) + e * 0.5) * beat * sr) + r.nextInt (200);
                    bool stab = (e == 0 || e == 3 || e == 6);
                    int len = (int) (sr * (stab ? 0.45 : 0.16));
                    for (int interval : { 0, 7, 12 })
                        pluck (b, start, midiToHz (root + interval), sr, stab ? 0.22f : 0.12f, stab ? 0.996f : 0.97f, len, r);
                }
            }
            return b;
        };

        juce::AudioBuffer<float> lead (1, total);
        lead.clear();
        const int melody[] = { 64, 67, 69, 71, 72, 71, 69, 67, 69, 64, 67, 62, 64, 64, 0, 0 };
        for (int i = 0; i < 16; ++i)
            if (melody[i] > 0)
                pluck (lead, (int) ((16 + i * 1.0) * beat * sr), midiToHz (melody[i]), sr, 0.3f, 0.9985f, (int) (sr * beat * 1.2), rng);

        juce::AudioBuffer<float> bass (1, total);
        bass.clear();
        for (int bar = 0; bar < bars; ++bar)
            for (int q = 0; q < 8; ++q)
                pluck (bass, (int) ((bar * 4 + q * 0.5) * beat * sr), midiToHz (roots[(bar / 2) % 4] - 12), sr, 0.35f, 0.995f, (int) (sr * beat * 0.5), rng);

        auto gtrL = folder.getChildFile ("Gtr Rhythm L.wav");
        auto gtrR = folder.getChildFile ("Gtr Rhythm R.wav");
        auto gtrLead = folder.getChildFile ("Gtr Lead.wav");
        auto bassFile = folder.getChildFile ("Bass DI.wav");
        writeWav (gtrL, makeRhythm (1), sr);
        writeWav (gtrR, makeRhythm (2), sr);
        writeWav (gtrLead, lead, sr);
        writeWav (bassFile, bass, sr);

        // Drum groove as a MIDI file (GM: 36 kick, 38 snare, 42 closed hat, 49 crash)
        juce::MidiMessageSequence seq;
        const int ppq = 960;
        auto addNote = [&] (int note, double beatPos, int vel)
        {
            seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) vel), beatPos * ppq);
            seq.addEvent (juce::MidiMessage::noteOff (10, note), (beatPos + 0.2) * ppq);
        };

        for (int bar = 0; bar < bars; ++bar)
        {
            double b0 = bar * 4.0;
            if (bar % 4 == 0) addNote (49, b0, 110);
            for (int e = 0; e < 8; ++e) addNote (42, b0 + e * 0.5, e % 2 == 0 ? 100 : 70);
            addNote (36, b0, 120); addNote (36, b0 + 1.5, 100); addNote (36, b0 + 2.0, 115);
            addNote (38, b0 + 1.0, 120); addNote (38, b0 + 3.0, 120);
            if (bar % 2 == 1) addNote (38, b0 + 3.75, 80);
        }
        seq.updateMatchedPairs();

        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (ppq);
        mf.addTrack (seq);
        auto drumsFile = folder.getChildFile ("Groove - Verse.mid");
        {
            drumsFile.deleteFile();
            juce::FileOutputStream os (drumsFile);
            mf.writeTo (os);
        }

        // Build the session
        auto editFile = folder.getChildFile ("Demo Song" + juce::String (te::editFileSuffix));
        {
            Session s (engine, editFile);
            s.setBpm (bpm);
            s.edit->ensureNumberOfAudioTracks (10);
            auto tracks = s.getTracks();

            const char* names[] = { "Gtr Rhythm L", "Gtr Rhythm R", "Gtr Lead", "Bass DI", "EZdrummer", "Scratch Vox", "Gtr Overdub", "Room Mic", "Track 9", "Track 10" };
            for (int i = 0; i < tracks.size(); ++i)
                tracks[i]->setName (names[i]);

            s.importFile (*tracks[0], gtrL, {});
            s.importFile (*tracks[1], gtrR, {});
            s.importFile (*tracks[2], gtrLead, {});
            s.importFile (*tracks[3], bassFile, {});
            s.importFile (*tracks[4], drumsFile, {});

            if (auto vp = tracks[0]->getVolumePlugin()) { vp->setPan (-0.8f); vp->setVolumeDb (-3.0f); }
            if (auto vp = tracks[1]->getVolumePlugin()) { vp->setPan (0.8f);  vp->setVolumeDb (-3.0f); }
            if (auto vp = tracks[3]->getVolumePlugin()) vp->setVolumeDb (-6.0f);
            tracks[2]->setSolo (false);
            tracks[5]->setMute (true);

            auto& t = s.edit->getTransport();
            t.setLoopRange ({ te::TimePosition(), s.edit->tempoSequence.toTime (te::BeatPosition::fromBeats (16.0)) });
            t.looping = true;
            t.setPosition (s.edit->tempoSequence.toTime (te::BeatPosition::fromBeats (21.0)));
            s.edit->clickTrackEnabled = true;
            s.save();
        }

        return editFile;
    }
}
