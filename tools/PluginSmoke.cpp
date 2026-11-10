// plugin-smoke — the `plugin-smoke` CTest. Drives the real plugin processor
// without a host:
//   * audio passes through unchanged (float and double), also when bypassed
//   * a fake playhead reaches getTransport(), with the loop pass counted
//   * the singer's display window opens, follows the track name, hides, and
//     is still there afterwards (owned by the processor, not the editor)
//   * the editor opens and closes while the display stays up
//   * the saved state reads back
// Needs a desktop session (on Linux: Xvfb plus a window manager such as
// openbox; on a bare X server JUCE fails with BadAtom when opening windows).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>

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

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    run();
    if (failures == 0)
        std::printf ("plugin-smoke: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
