#pragma once

#include <juce_graphics/juce_graphics.h>

#include "Song.h"

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

// Song parts: intro yellow, verse blue, chorus red, pre-chorus magenta,
// bridge purple, outro orange, anything else grey.
inline juce::Colour sectionColour (SectionKind kind)
{
    switch (kind)
    {
        case SectionKind::intro:     return juce::Colour (0xffe8c020);
        case SectionKind::verse:     return juce::Colour (0xff2f80e8);
        case SectionKind::prechorus: return juce::Colour (0xffd83ab0);
        case SectionKind::chorus:    return juce::Colour (0xffe5403a);
        case SectionKind::bridge:    return juce::Colour (0xff8c52e6);
        case SectionKind::outro:     return juce::Colour (0xfff08a24);
        case SectionKind::other:     break;
    }
    return juce::Colour (0xff6b7480);
}

// Text that reads on the full section colour.
inline juce::Colour sectionInk (SectionKind kind)
{
    return kind == SectionKind::intro || kind == SectionKind::outro ? juce::Colour (0xff15171a) : text;
}

}  // namespace boothcue::theme
