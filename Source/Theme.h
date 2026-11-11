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

// Live's 70 clip colours (Live 10-12), by the index a clip stores in the set:
// 14 columns x 5 rows, row by row. Values sampled from Live's colour picker
// (github.com/danhemerlein/ableton-colors).
inline juce::Colour liveClipColour (int index)
{
    static const juce::uint32 palette[70] = {
        0xfffd95a7, 0xfffda43a, 0xffcb9834, 0xfff7f384, 0xffc0f932, 0xff32fd42, 0xff39fdaa,
        0xff65fee8, 0xff8dc6fd, 0xff5682e1, 0xff93a9fc, 0xffd670e2, 0xffe3569f, 0xffffffff,
        0xfffc393d, 0xfff46c20, 0xff98714e, 0xfffeee4a, 0xff8bfd70, 0xff44c121, 0xff1ebeaf,
        0xff31e9fd, 0xff22a5eb, 0xff127ebe, 0xff8870e1, 0xffb579c4, 0xfffd42d2, 0xffd0d0d0,
        0xffe0685d, 0xfffda378, 0xffd2ac75, 0xffedfeb2, 0xffd2e39c, 0xffbacf79, 0xff9cc38f,
        0xffd5fde2, 0xffcef1f8, 0xffb9c2e2, 0xffcdbce3, 0xffae9ae3, 0xffe5dce1, 0xffa9a9a9,
        0xffc5928c, 0xffb68259, 0xff98836b, 0xffbfb96e, 0xffa6bc25, 0xff7eaf52, 0xff8ac2ba,
        0xff9cb3c3, 0xff86a5c1, 0xff8494ca, 0xffa596b4, 0xffbea0bd, 0xffbb7296, 0xff7b7b7b,
        0xffad3436, 0xffa75135, 0xff714f42, 0xffdac229, 0xff85952b, 0xff559e38, 0xff1b9b8e,
        0xff266383, 0xff1b3393, 0xff3154a0, 0xff624eab, 0xffa24eab, 0xffca326e, 0xff3c3c3c
    };
    return index >= 0 && index < 70 ? juce::Colour (palette[index]) : juce::Colour (0xff6b7480);
}

// A song part's colour: the colour of its clip in Live when known (so the
// display matches the set exactly), else one by kind of part.
inline juce::Colour partColour (const Section& section)
{
    return section.color >= 0 && section.color < 70 ? liveClipColour (section.color)
                                                     : sectionColour (sectionKind (section.name));
}

// Text that reads on a full part colour: dark on light colours.
inline juce::Colour inkOn (juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.62f ? juce::Colour (0xff15171a) : text;
}

inline juce::Colour sectionInk (SectionKind kind)
{
    return inkOn (sectionColour (kind));
}

}  // namespace boothcue::theme
