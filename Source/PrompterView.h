#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PrompterPainter.h"

#include <functional>

namespace boothcue
{

class BoothCueAudioProcessor;

// The live view in the display window: a 30 Hz timer reads the processor and
// repaints only when what is shown changes.
//   F or double-click: full screen on / off      Esc: leave full screen
//   T or space: tap the next line            Backspace: undo the last tap
class PrompterView final : public juce::Component,
                           private juce::Timer
{
public:
    explicit PrompterView (BoothCueAudioProcessor&);
    ~PrompterView() override;

    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    std::function<void()> onTap, onUndo;

private:
    void timerCallback() override;
    void toggleFullScreen();

    // What was last drawn, to skip repaints that change nothing.
    struct Shown
    {
        bool hasPosition = false, playing = false, recording = false, looping = false, loopShown = false;
        int bar = 0, beat = 0, section = -1, nextSection = -1, currentCue = -1, nextCue = -1;
        int countKind = 0, countValue = 0, loopPass = 0, playheadX = -1, nextSectionIn = -1;
        double tapNoteAt = 0.0;
        juce::uint32 contentVersion = 0;
        int width = 0, height = 0;
        bool operator== (const Shown&) const;
    };

    BoothCueAudioProcessor& processor;
    PrompterPainter painter;
    PrompterScene scene;
    Shown shown;
    bool first = true;
    juce::String titleTrack { "\x01" };   // never a real name: forces the first title update
};

}  // namespace boothcue
