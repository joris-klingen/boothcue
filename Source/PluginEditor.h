#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace boothcue
{

// The settings window inside Live. For now: open / hide the singer's display,
// the display toggles, and a transport readout to check a build in the host.
// TODO step 4 adds linking the set, the lyrics and tap-along.
class BoothCueAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit BoothCueAudioProcessorEditor (BoothCueAudioProcessor&);
    ~BoothCueAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void pushSettings();
    void pullSettings();

    BoothCueAudioProcessor& processor;
    TransportState shown;

    juce::TextButton displayButton;
    juce::ToggleButton showPart   { "Song part" };
    juce::ToggleButton showLyrics { "Lyrics" };
    juce::ToggleButton showChords { "Chords (coming)" };
    juce::ToggleButton showBass   { "Bass note (coming)" };
    juce::Label  sizeLabel { {}, "Text size" };
    juce::Slider sizeSlider;

    juce::Rectangle<int> readoutArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessorEditor)
};

}  // namespace boothcue
