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
// 14 columns x 5 rows, row by row. The values are Live's own: the RGB table of
// its Push script (MIDI Remote Scripts/pushbase/colors.pyc in Live 12.4), not
// colours sampled from a screenshot.
inline juce::Colour liveClipColour (int index)
{
    static const juce::uint32 palette[70] = {
        0xffff94a6, 0xffffa529, 0xffcc9927, 0xfff7f47c, 0xffbffb00, 0xff1aff2f, 0xff25ffa8,
        0xff5cffe8, 0xff8bc5ff, 0xff5480e4, 0xff92a7ff, 0xffd86ce4, 0xffe553a0, 0xffffffff,
        0xffff3636, 0xfff66c03, 0xff99724b, 0xfffff034, 0xff87ff67, 0xff3dc300, 0xff00bfaf,
        0xff19e9ff, 0xff10a4ee, 0xff007dc0, 0xff886ce4, 0xffb677c6, 0xffff39d4, 0xffd0d0d0,
        0xffe2675a, 0xffffa374, 0xffd3ad71, 0xffedffae, 0xffd2e498, 0xffbad074, 0xff9bc48d,
        0xffd4fde1, 0xffcdf1f8, 0xffb9c1e3, 0xffcdbbe4, 0xffae98e5, 0xffe5dce1, 0xffa9a9a9,
        0xffc6928b, 0xffb78256, 0xff99836a, 0xffbfba69, 0xffa6be00, 0xff7db04d, 0xff88c2ba,
        0xff9bb3c4, 0xff85a5c2, 0xff8393cc, 0xffa595b5, 0xffbf9fbe, 0xffbc7196, 0xff7b7b7b,
        0xffaf3333, 0xffa95131, 0xff724f41, 0xffdbc300, 0xff85961f, 0xff539f31, 0xff0a9c8e,
        0xff236384, 0xff1a2f96, 0xff2f52a2, 0xff624bad, 0xffa34bad, 0xffcc2e6e, 0xff3c3c3c
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
