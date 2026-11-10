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

    // With BOOTHCUE_EDITOR_PNG set: a snapshot of the editor with a linked set,
    // for reviewing the layout without a host.
    void editorSnapshot (const juce::File& fixtures)
    {
        const auto out = juce::SystemStats::getEnvironmentVariable ("BOOTHCUE_EDITOR_PNG", {});
        if (out.isEmpty())
            return;
        BoothCueAudioProcessor p;
        p.linkSet (fixtures.getChildFile ("fallback.als"));
        waitFor (p, State::ok);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        editor->setVisible (true);
        pump (100);
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::File file (out);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat png;
        EXPECT (stream.openedOk() && png.writeImageToStream (image, stream));
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
        editorSnapshot (fixtures);
    }
    else
        std::printf ("plugin-smoke: no fixtures dir given, skipping the link checks\n");
    if (failures == 0)
        std::printf ("plugin-smoke: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
