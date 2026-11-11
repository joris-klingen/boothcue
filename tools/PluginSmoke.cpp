// plugin-smoke — the `plugin-smoke` CTest. Drives the real plugin processor
// without a host:
//   * audio passes through unchanged (float and double), also when bypassed
//   * a fake playhead reaches getTransport(), with the loop pass counted
//   * the singer's display window opens, follows the track name, hides, and
//     is still there afterwards (owned by the processor, not the editor)
//   * the editor opens and closes while the display stays up
//   * linking a set reads its sections; a save is picked up; the link and the
//     last structure read are saved with the plugin and come back, also when
//     the file is gone or the state arrives off the message thread
// Needs a desktop session (on Linux: Xvfb plus a window manager such as
// openbox; on a bare X server JUCE fails with BadAtom when opening windows).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "LyricImport.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <thread>

using namespace boothcue;

namespace
{
    int failures = 0;

    void expect (bool ok, const char* what, int line)
    {
        if (! ok)
        {
            std::printf ("FAIL (line %d): %s\n", line, what);
            ++failures;
        }
    }
    #define EXPECT(cond) expect ((cond), #cond, __LINE__)

    struct FakePlayHead final : juce::AudioPlayHead
    {
        juce::AudioPlayHead::PositionInfo info;
        juce::Optional<PositionInfo> getPosition() const override { return info; }
    };

    template <typename Sample>
    bool passesThrough (BoothCueAudioProcessor& p, bool bypassed)
    {
        juce::AudioBuffer<Sample> buffer (2, 256);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                buffer.setSample (c, i, static_cast<Sample> (std::sin (0.01 * i + c)));
        juce::AudioBuffer<Sample> original (buffer);
        juce::MidiBuffer midi;
        if (bypassed)
            p.processBlockBypassed (buffer, midi);
        else
            p.processBlock (buffer, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                if (! juce::exactlyEqual (buffer.getSample (c, i), original.getSample (c, i)))
                    return false;
        return true;
    }

    void pump (int ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    using State = BoothCueAudioProcessor::LinkStatus::State;

    // Pumps the message loop until the link status is `want` (or ~4 s pass).
    bool waitFor (const BoothCueAudioProcessor& p, State want)
    {
        for (int i = 0; i < 80 && p.getLinkStatus().state != want; ++i)
            pump (50);
        return p.getLinkStatus().state == want;
    }

    juce::MemoryBlock stateOf (BoothCueAudioProcessor& p)
    {
        juce::MemoryBlock m;
        p.getStateInformation (m);
        return m;
    }

    void linkTests (const juce::File& fixtures)
    {
        const auto dir = juce::File::createTempFile ("boothcue-link");
        EXPECT (dir.createDirectory());
        const auto set = dir.getChildFile ("Song.als");
        EXPECT (fixtures.getChildFile ("parts.als").copyFileTo (set));

        BoothCueAudioProcessor p;
        EXPECT (p.getLinkStatus().state == State::none && p.getSong().sections().empty());

        p.linkSet (set);
        EXPECT (p.getLinkStatus().state == State::reading);
        EXPECT (waitFor (p, State::ok));
        EXPECT (p.getSong().sections().size() == 8);
        EXPECT (p.getLinkStatus().sections == 8 && p.getLinkStatus().trackName == "Parts");
        EXPECT (p.getSong().sections()[1].color == 1);   // Live clip colours come along
        EXPECT (! p.getLinkStatus().usedFallback);

        // Lyrics stay linked across a re-read: they belong to the song, not the file.
        Song withLyrics = p.getSong();
        withLyrics.setBlocks ({ LyricBlock { "Chorus", { LyricLine { "la", -1.0 } } } });
        p.setSong (withLyrics);
        EXPECT (! p.getSong().cues().empty());

        // A save in Live with another structure.
        pump (100);
        EXPECT (fixtures.getChildFile ("fallback.als").copyFileTo (set));
        EXPECT (set.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5)));
        for (int i = 0; i < 80 && p.getSong().sections().size() != 2; ++i)
            pump (50);
        EXPECT (p.getSong().sections().size() == 2);
        EXPECT (p.getLinkStatus().usedFallback && p.getLinkStatus().trackName == "Structure");
        EXPECT (p.getSong().blocks().size() == 1);

        // Back to the real structure, then save the plugin state.
        EXPECT (fixtures.getChildFile ("parts.als").copyFileTo (set));
        EXPECT (set.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (10)));
        for (int i = 0; i < 80 && p.getSong().sections().size() != 8; ++i)
            pump (50);
        EXPECT (p.getSong().sections().size() == 8);
        const auto saved = stateOf (p);

        // Restored on the message thread: the saved structure shows at once,
        // then the file is read again.
        {
            BoothCueAudioProcessor q;
            q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            EXPECT (q.getSong().sections().size() == 8);
            EXPECT (q.getLinkStatus().file == set);
            EXPECT (waitFor (q, State::ok));
            EXPECT (q.getSong().sections()[1].name == "Verse 1");
        }

        // Restored where the file is missing: the saved structure stays.
        EXPECT (set.deleteFile());
        {
            BoothCueAudioProcessor q;
            q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            EXPECT (waitFor (q, State::missing));
            EXPECT (q.getSong().sections().size() == 8);
            EXPECT (q.getSong().sections()[1].color == 1);   // saved with the state
            EXPECT (q.getLinkStatus().sections == 8);
            // Its own state still points at the file, so it relinks when it's back.
            BoothCueAudioProcessor r;
            const auto again = stateOf (q);
            r.setStateInformation (again.getData(), static_cast<int> (again.getSize()));
            EXPECT (r.getLinkStatus().file == set);
        }

        // Restored from another thread (hosts do that): applied by the timer,
        // and handed back unchanged if asked for before that.
        {
            BoothCueAudioProcessor q;
            std::thread t ([&] { q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize())); });
            t.join();
            const auto echoed = stateOf (q);
            EXPECT (echoed.getSize() == saved.getSize());
            for (int i = 0; i < 40 && q.getSong().sections().empty(); ++i)
                pump (25);
            EXPECT (q.getSong().sections().size() == 8);
            EXPECT (q.getLinkStatus().file == set);
        }

        // Lyrics and their taps travel with the state.
        {
            auto blocks = parseLyrics ("[Verse 1]\nfirst line\nsecond line\n[Chorus]\nhold on\n");
            blocks[0].lines[1].offset = 9.5;
            p.setLyrics (blocks);
            EXPECT (p.getSong().blocks().size() == 2);
            EXPECT (p.getSong().cues().size() == 2 + 3);   // the chorus is sung three times
            const auto savedLyrics = stateOf (p);
            BoothCueAudioProcessor q;
            q.setStateInformation (savedLyrics.getData(), static_cast<int> (savedLyrics.getSize()));
            const auto& b = q.getSong().blocks();
            EXPECT (b.size() == 2 && b[0].header == "Verse 1" && b[1].lines.size() == 1);
            EXPECT (b.size() == 2 && ! b[0].lines[0].timed() && std::abs (b[0].lines[1].offset - 9.5) < 1e-9);
            EXPECT (q.getSong().cues().size() == 5);
            // Unicode survives.
            p.setLyrics (parseLyrics ("[Chorus]\nzo\xC3\xAB \xE2\x80\x94 caf\xC3\xA9\n"));
            const auto unicode = stateOf (p);
            BoothCueAudioProcessor r;
            r.setStateInformation (unicode.getData(), static_cast<int> (unicode.getSize()));
            EXPECT (r.getSong().blocks().size() == 1
                    && r.getSong().blocks()[0].lines[0].text == "zo\xC3\xAB \xE2\x80\x94 caf\xC3\xA9");
            p.setLyrics ({});
            EXPECT (p.getSong().blocks().empty());
        }

        // Unlink: structure gone, nothing saved.
        p.unlinkSet();
        EXPECT (p.getLinkStatus().state == State::none && p.getSong().sections().empty());
        {
            BoothCueAudioProcessor q;
            const auto empty = stateOf (p);
            q.setStateInformation (empty.getData(), static_cast<int> (empty.getSize()));
            EXPECT (q.getSong().sections().empty() && q.getLinkStatus().file == juce::File());
        }

        dir.deleteRecursively();
    }

    // Runs one audio block with the playhead at ppq.
    void playAt (BoothCueAudioProcessor& p, FakePlayHead& head, double ppq, bool playing = true)
    {
        head.info.setPpqPosition (ppq);
        head.info.setIsPlaying (playing);
        juce::AudioBuffer<float> buffer (2, 64);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
    }

    void tapTests (const juce::File& fixtures)
    {
        BoothCueAudioProcessor p;
        p.linkSet (fixtures.getChildFile ("parts.als"));   // Intro 0-16, Verse 1 16-48, Chorus 48-80, ...
        EXPECT (waitFor (p, State::ok));
        p.setLyrics (parseLyrics ("[Verse 1]\none\ntwo\nthree\n[Chorus]\nhold on\nhold on again\n"));

        FakePlayHead head;
        head.info.setBpm (96.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 64);

        // Untimed lines sit on the bar grid: 3 lines over 8 bars -> bars 5, 8, 10.
        pump (150);
        EXPECT (std::abs (p.getSong().barLength() - 4.0) < 1e-9);
        EXPECT (std::abs (p.getSong().resolvedOffset (0, 1) - 12.0) < 1e-9);

        BoothCueAudioProcessor::TapSettings ts;
        ts.snapBeats = 0.25;
        p.setTapSettings (ts);

        // Not in a section with lyrics.
        playAt (p, head, 5.0);
        EXPECT (! p.tap());
        EXPECT (! p.wasLastTapOk() && p.getLastTapMessage().contains ("Intro"));

        // Verse 1: lines stamped one after another.
        playAt (p, head, 17.0);
        EXPECT (p.tap());
        playAt (p, head, 33.05);
        EXPECT (p.tap());
        const auto& verse = p.getSong().blocks()[0].lines;
        EXPECT (std::abs (verse[0].offset - 1.0) < 1e-9);
        EXPECT (std::abs (verse[1].offset - 17.0) < 1e-9);
        EXPECT (p.getLastTapMessage().contains ("two") && p.getLastTapMessage().contains ("9.2.1"));
        EXPECT (p.getTapCursor().block == 0 && p.getTapCursor().line == 2);

        // Undo puts the line back and the cursor on it.
        EXPECT (p.canUndo());
        p.undo();
        EXPECT (! p.getSong().blocks()[0].lines[1].timed() && p.getTapCursor().line == 1);
        EXPECT (! p.canUndo());

        // Nudge and clear, each undoable.
        p.nudgeLine (0, 0, 0.25);
        EXPECT (std::abs (p.getSong().blocks()[0].lines[0].offset - 1.25) < 1e-9);
        p.undo();
        EXPECT (std::abs (p.getSong().blocks()[0].lines[0].offset - 1.0) < 1e-9);
        p.clearAllTimings();
        EXPECT (! p.getSong().blocks()[0].lines[0].timed());
        p.undo();
        EXPECT (p.getSong().blocks()[0].lines[0].timed());
        p.clearLine (0, 0);
        EXPECT (! p.getSong().blocks()[0].lines[0].timed());

        // Latency correction: 100 ms at 96 BPM = 0.16 beat earlier (no snap).
        ts.snapBeats = 0.0;
        ts.latencyMs = 100.0;
        p.setTapSettings (ts);
        p.setTapCursor (1, 0);
        playAt (p, head, 50.0);
        EXPECT (p.tap());
        const double off = p.getSong().blocks()[1].lines[0].offset;
        EXPECT (off > 2.0 - 0.16 - 1e-6 && off < 2.0 - 0.16 + 0.05);   // + the few ms since the block

        // Stopped: placed exactly at the playhead, no latency taken off.
        p.setTapCursor (1, 1);
        playAt (p, head, 60.0, false);
        EXPECT (p.tap());
        EXPECT (std::abs (p.getSong().blocks()[1].lines[1].offset - 12.0) < 1e-9);

        // The grid follows the host's time signature.
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 3, 4 });
        playAt (p, head, 60.0, false);
        pump (150);
        EXPECT (std::abs (p.getSong().barLength() - 3.0) < 1e-9);

        // Taps are saved.
        juce::MemoryBlock m;
        p.getStateInformation (m);
        BoothCueAudioProcessor q;
        q.setStateInformation (m.getData(), static_cast<int> (m.getSize()));
        EXPECT (std::abs (q.getSong().blocks()[1].lines[1].offset - 12.0) < 1e-9);

        p.setPlayHead (nullptr);
    }

    // Chords detected from a linked set's MIDI, saved, and restored.
    void chordTests (const juce::File& fixtures)
    {
        BoothCueAudioProcessor p;
        p.linkSet (fixtures.getChildFile ("notes.als"));
        EXPECT (waitFor (p, State::ok));
        const auto& song = p.getSong();
        EXPECT (song.chords().size() >= 4);
        EXPECT (song.chordAt (1.0) >= 0 && chordName (song.chords()[static_cast<std::size_t> (song.chordAt (1.0))].chord, song.key()) == "C");
        EXPECT (p.getLinkStatus().chordSummary.contains ("Keys") && p.getLinkStatus().chordSummary.contains ("Bass"));

        juce::MemoryBlock m;
        p.getStateInformation (m);
        BoothCueAudioProcessor q;
        q.setStateInformation (m.getData(), static_cast<int> (m.getSize()));
        EXPECT (q.getSong().chords().size() == song.chords().size());
        if (q.getSong().chords().size() == song.chords().size() && ! song.chords().empty())
            EXPECT (q.getSong().chords()[0].chord == song.chords()[0].chord && q.getSong().key().tonic == song.key().tonic);
        EXPECT (q.getLinkStatus().chordSummary == p.getLinkStatus().chordSummary);

        // The chords database: a chart makes the chords "edited"; detection
        // after a save no longer replaces them.
        EXPECT (! p.areChordsEdited() && p.hasDetectedChords());
        const auto chart = p.getChordChart();
        EXPECT (chart.contains ("[Verse]") && chart.contains ("[Chorus]"));
        auto import = p.setChordChart ("[Verse]\n| G | G | D/F# | Em |\n[Chorus]\n| C |\n");
        EXPECT (import.partsFilled == 2 && import.unknown.empty());
        EXPECT (p.areChordsEdited());
        auto nameAt = [] (const BoothCueAudioProcessor& proc, double t)
        {
            const int i = proc.getSong().chordAt (t);
            return i < 0 ? std::string ("-") : chordName (proc.getSong().chords()[static_cast<std::size_t> (i)].chord, proc.getSong().key());
        };
        EXPECT (nameAt (p, 1) == "G" && nameAt (p, 9) == "D/F#" && nameAt (p, 17) == "C" && nameAt (p, 21) == "-");
        // A save in Live: detected again, kept aside.
        const auto set = fixtures.getChildFile ("notes.als");
        const auto copy = juce::File::createTempFile (".als");
        EXPECT (set.copyFileTo (copy));
        p.linkSet (copy);
        EXPECT (waitFor (p, State::ok));
        EXPECT (p.areChordsEdited() && nameAt (p, 1) == "G");
        EXPECT (copy.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5)));
        pump (400);
        EXPECT (p.areChordsEdited() && nameAt (p, 1) == "G");
        // Saved with the set, edited flag and all.
        {
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            BoothCueAudioProcessor r;
            r.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
            EXPECT (r.areChordsEdited() && nameAt (r, 9) == "D/F#");
        }
        // Back to the detected chords, and undo that.
        p.useDetectedChords();
        EXPECT (! p.areChordsEdited() && nameAt (p, 1) == "C");
        EXPECT (p.canUndoChords());
        p.undoChords();
        EXPECT (p.areChordsEdited() && nameAt (p, 1) == "G");
        // Lyrics with their timings, for export.
        p.setLyrics (parseLyrics ("[Verse]\none\ntwo\n"));
        Song withTap = p.getSong();
        EXPECT (withTap.stamp (0, 1, 9.0));
        p.setSong (withTap);
        EXPECT (p.getTimedLyrics().contains ("@3.2.1 two"));
        copy.deleteFile();

        // Unlinking clears them.
        p.unlinkSet();
        EXPECT (p.getSong().chords().empty() && ! p.areChordsEdited());
    }

    // Step 6: display / tap settings, the display window and the editor size
    // come back with the state.
    void settingsTests()
    {
        juce::MemoryBlock saved;
        {
            BoothCueAudioProcessor p;
            DisplaySettings d;
            d.showSongPart = false;
            d.showChords = false;
            d.fontScale = 1.4f;
            d.count.enabled = true;
            d.count.countInBars = 4;
            d.count.entryGapBars = 2.0;
            p.setDisplaySettings (d);
            BoothCueAudioProcessor::TapSettings t;
            t.snapBeats = 0.5;
            t.latencyMs = 45.0;
            p.setTapSettings (t);
            p.setEditorSize ({ 1200, 800 });

            p.showDisplay();
            pump (150);
            auto* w = dynamic_cast<juce::ResizableWindow*> (juce::TopLevelWindow::getActiveTopLevelWindow());
            for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                if (juce::TopLevelWindow::getTopLevelWindow (i)->getName().startsWith ("BoothCue"))
                    w = dynamic_cast<juce::ResizableWindow*> (juce::TopLevelWindow::getTopLevelWindow (i));
            EXPECT (w != nullptr);
            if (w != nullptr)
                w->setBounds (100, 120, 900, 500);
            pump (150);
            p.getStateInformation (saved);
            const auto tree = juce::ValueTree::readFromData (saved.getData(), saved.getSize());
            EXPECT (tree.getChildWithName ("Display")["placement"].toString() == "100 120 900 500");
        }

        BoothCueAudioProcessor q;
        q.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        pump (150);
        const auto& d = q.getDisplaySettings();
        EXPECT (! d.showSongPart && d.showLyrics && ! d.showChords && d.showBassNote);
        EXPECT (std::abs (d.fontScale - 1.4f) < 1e-5f);
        EXPECT (d.count.enabled && d.count.countInBars == 4 && std::abs (d.count.entryGapBars - 2.0) < 1e-9);
        EXPECT (std::abs (q.getTapSettings().snapBeats - 0.5) < 1e-9 && std::abs (q.getTapSettings().latencyMs - 45.0) < 1e-9);
        EXPECT (q.getEditorSize() == juce::Point<int> (1200, 800));
        EXPECT (q.isDisplayVisible());   // it was open: it opens again
        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
            if (auto* w = juce::TopLevelWindow::getTopLevelWindow (i); w->getName().startsWith ("BoothCue") && w->isVisible())
            {
                if (! (w->getWidth() == 900 && w->getHeight() == 500))
                    std::printf ("restored window: %s\n", w->getBounds().toString().toRawUTF8());
                EXPECT (w->getWidth() == 900 && w->getHeight() == 500);
            }

        // Closed when saved: stays closed; and an older state without these
        // parts gives the defaults.
        q.hideDisplay();
        juce::MemoryBlock closed;
        q.getStateInformation (closed);
        BoothCueAudioProcessor r;
        r.setStateInformation (closed.getData(), static_cast<int> (closed.getSize()));
        EXPECT (! r.isDisplayVisible());
        juce::ValueTree old ("BoothCue");
        old.setProperty ("version", 1, nullptr);
        juce::MemoryBlock oldState;
        {
            juce::MemoryOutputStream out (oldState, false);
            old.writeToStream (out);
        }
        BoothCueAudioProcessor s;
        s.setStateInformation (oldState.getData(), static_cast<int> (oldState.getSize()));
        EXPECT (s.getDisplaySettings().showSongPart && std::abs (s.getDisplaySettings().fontScale - 1.0f) < 1e-6f);
        EXPECT (s.getTapSettings().latencyMs < 1e-9 && ! s.isDisplayVisible());
        pump (50);
    }

    // With BOOTHCUE_EDITOR_PNG set: a snapshot of the editor with a linked set,
    // for reviewing the layout without a host.
    void editorSnapshot (const juce::File& fixtures)
    {
        const auto out = juce::SystemStats::getEnvironmentVariable ("BOOTHCUE_EDITOR_PNG", {});
        if (out.isEmpty())
            return;
        BoothCueAudioProcessor p;
        p.linkSet (fixtures.getChildFile ("parts.als"));
        waitFor (p, State::ok);
        auto blocks = parseLyrics ("[Verse 1]\nStreetlights hum a song I used to know\nEvery window glowing soft and low\n"
                                   "[Chorus]\nHold on, hold on\nWe're running out of reasons to be wrong\n"
                                   "[Verse 2]\nPaper boats are sailing down the drain\n[Bridge]\nOh-oh\n");
        blocks[1].lines[1].offset = 9.5;
        p.setLyrics (blocks);

        // A position in the first chorus, so its line shows as being sung.
        FakePlayHead head;
        head.info.setPpqPosition (60.0);
        head.info.setBpm (96.0);
        head.info.setIsPlaying (true);
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        p.setTapCursor (1, 1);
        p.tap();
        p.setPlayHead (nullptr);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setVisible (true);
        pump (100);
        auto snapshot = [&] (const juce::File& file)
        {
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat png;
            EXPECT (stream.openedOk() && png.writeImageToStream (image, stream));
        };
        juce::File file (out);
        snapshot (file);
        // The Chords tab too.
        for (auto* child : editor->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->getButtonText() == "Chords")
                b->triggerClick();
        pump (150);
        snapshot (file.getSiblingFile (file.getFileNameWithoutExtension() + "-chords.png"));
    }

    void run()
    {
        auto processor = std::make_unique<BoothCueAudioProcessor>();
        auto& p = *processor;

        EXPECT (p.getTotalNumInputChannels() == 2 && p.getTotalNumOutputChannels() == 2);
        EXPECT (! p.getTransport().valid);

        p.prepareToPlay (48000.0, 256);
        FakePlayHead head;
        p.setPlayHead (&head);

        // No position from the host yet.
        EXPECT (passesThrough<float> (p, false));
        EXPECT (! p.getTransport().valid);

        head.info.setPpqPosition (17.5);
        head.info.setBpm (96.0);
        head.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 6, 8 });
        head.info.setIsPlaying (true);
        head.info.setIsRecording (true);
        head.info.setIsLooping (true);
        head.info.setLoopPoints (juce::AudioPlayHead::LoopPoints { 16.0, 32.0 });
        head.info.setPpqPositionOfLastBarStart (15.0);

        EXPECT (passesThrough<float> (p, false));
        auto t = p.getTransport();
        EXPECT (t.valid && t.playing && t.recording && t.looping);
        EXPECT (std::abs (t.ppq - 17.5) < 1e-9 && std::abs (t.bpm - 96.0) < 1e-9);
        EXPECT (t.sigNum == 6 && t.sigDen == 8);
        EXPECT (std::abs (t.loopStart - 16.0) < 1e-9 && std::abs (t.loopEnd - 32.0) < 1e-9);
        EXPECT (t.hasLastBarStart && std::abs (t.lastBarStart - 15.0) < 1e-9);
        EXPECT (t.loopPass == 1);

        // Wrap the loop while bypassed and in double precision: still followed.
        head.info.setPpqPosition (31.5);
        EXPECT (passesThrough<double> (p, true));
        head.info.setPpqPosition (16.1);
        EXPECT (passesThrough<double> (p, false));
        t = p.getTransport();
        EXPECT (t.loopPass == 2 && std::abs (t.ppq - 16.1) < 1e-9);
        head.info.setIsPlaying (false);
        EXPECT (passesThrough<float> (p, true));
        EXPECT (p.getTransport().loopPass == 0);

        // The display window: owned by the processor, shown / hidden on request.
        EXPECT (! p.isDisplayVisible());
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String ("Lead Vox");
        p.updateTrackProperties (props);
        p.showDisplay();
        pump (200);
        EXPECT (p.isDisplayVisible());

        auto* window = dynamic_cast<juce::DocumentWindow*> (
            juce::TopLevelWindow::getActiveTopLevelWindow());
        bool found = false;
        for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
        {
            auto* w = juce::TopLevelWindow::getTopLevelWindow (i);
            if (w->getName().startsWith ("BoothCue"))
            {
                found = true;
                window = dynamic_cast<juce::DocumentWindow*> (w);
                EXPECT (w->getName() == "BoothCue - Lead Vox");
                EXPECT (w->getWidth() >= 480 && w->getHeight() >= 270);
            }
        }
        EXPECT (found && window != nullptr);

        // Settings changes reach it and bump the content version.
        const auto version = p.getContentVersion();
        auto s = p.getDisplaySettings();
        s.showLyrics = false;
        s.fontScale = 5.0f;
        p.setDisplaySettings (s);
        EXPECT (p.getContentVersion() != version);
        EXPECT (! p.getDisplaySettings().showLyrics);
        EXPECT (std::abs (p.getDisplaySettings().fontScale - 2.0f) < 1e-6f);   // clamped

        // The editor comes and goes; the display stays.
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            EXPECT (editor != nullptr);
            editor->setVisible (true);
            editor->addToDesktop (0);
            pump (100);
        }
        pump (50);
        EXPECT (p.isDisplayVisible());

        // Close = hide; showing again reuses it.
        if (window != nullptr)
            window->closeButtonPressed();
        pump (50);
        EXPECT (! p.isDisplayVisible());
        p.showDisplay();
        pump (50);
        EXPECT (p.isDisplayVisible());
        p.hideDisplay();
        EXPECT (! p.isDisplayVisible());

        // State: written, read back, foreign data ignored.
        juce::MemoryBlock state;
        p.getStateInformation (state);
        EXPECT (state.getSize() > 0);
        BoothCueAudioProcessor other;
        other.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        const char junk[] = "not a state";
        other.setStateInformation (junk, sizeof (junk));

        p.setPlayHead (nullptr);
        p.showDisplay();   // deleting the processor with the display open must be clean
        pump (50);
        processor.reset();
        pump (50);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    run();
    if (argc > 1)
    {
        const auto fixtures = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
        linkTests (fixtures);
        tapTests (fixtures);
        chordTests (fixtures);
        settingsTests();
        editorSnapshot (fixtures);
    }
    else
        std::printf ("plugin-smoke: no fixtures dir given, skipping the link checks\n");
    if (failures == 0)
        std::printf ("plugin-smoke: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
