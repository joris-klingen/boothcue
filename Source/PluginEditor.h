#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace boothcue
{

// The settings window inside Live. For now a transport readout, so a build can
// be checked in the host: it should follow Live's position, tempo, signature,
// play / record state and loop. TODO step 4 turns this into the real editor
// (link set, lyrics, tap-along, display settings).
class BoothCueAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit BoothCueAudioProcessorEditor (BoothCueAudioProcessor&);
    ~BoothCueAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    BoothCueAudioProcessor& processor;
    TransportState shown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessorEditor)
};

}  // namespace boothcue
