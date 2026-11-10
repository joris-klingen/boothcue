#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace boothcue
{

class BoothCueAudioProcessor;

// The singer's display: a top-level window of its own, separate from the plugin
// editor, so it can live on a second monitor (full screen) while the Live
// window shows the editor or nothing at all.
//
// The processor owns it: it stays open when Live closes the plugin editor.
// Closing it only hides it. Message thread only.
class DisplayWindow final : public juce::DocumentWindow
{
public:
    explicit DisplayWindow (BoothCueAudioProcessor&);

    void closeButtonPressed() override;
    void setTrackName (const juce::String&);

    void moved() override;
    void resized() override;
    // Moved, resized, or in / out of full screen: the saved state follows.
    std::function<void()> onPlacementChanged;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DisplayWindow)
};

}  // namespace boothcue
