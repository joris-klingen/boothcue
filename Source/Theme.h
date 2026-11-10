#pragma once

#include <juce_graphics/juce_graphics.h>

namespace boothcue::theme
{

// Dark and high-contrast: the singer reads this from a distance, in a dim booth.
inline const juce::Colour background    { 0xff0a0c0f };
inline const juce::Colour text          { 0xfff4f6f8 };
inline const juce::Colour dim           { 0xff8a94a1 };
inline const juce::Colour faint         { 0xff3a414b };
inline const juce::Colour sectionPast   { 0xff191d23 };
inline const juce::Colour sectionFuture { 0xff2b313a };
inline const juce::Colour sectionNow    { 0xff2f8fff };
inline const juce::Colour count         { 0xffffd23f };
inline const juce::Colour record        { 0xffff453a };
inline const juce::Colour play          { 0xff32d74b };
inline const juce::Colour loop          { 0xffffa53a };

}  // namespace boothcue::theme
