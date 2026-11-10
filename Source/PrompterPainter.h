#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Prompter.h"

namespace boothcue
{

// Draws a PrompterScene. Shared by the display window and tools/PrompterRender,
// so what the render tool shows is what the singer sees.
//
// Every size follows the window: one unit is 1/100 of the height (or 1/160 of
// the width on a tall window), times the font-scale setting.
class PrompterPainter
{
public:
    void paint (juce::Graphics&, juce::Rectangle<int> bounds,
                const PrompterScene&, const Song&, const DisplaySettings&);

    // Where the playhead is drawn, in pixels from the left of `bounds` (-1: not
    // drawn). The view repaints when this moves, not on every timer tick.
    static int playheadX (juce::Rectangle<int> bounds, const PrompterScene&, const DisplaySettings&);

private:
    struct Fit
    {
        juce::String text;
        int width = 0, height = 0, maxLines = 0;
        float maxFont = 0.0f, minFont = 0.0f;
        bool bold = false;
        float font = 0.0f;
    };

    // Draws `text` wrapped into `area` (centred), at the largest font size from
    // maxFont down to minFont that fits in maxLines lines.
    void drawFitted (juce::Graphics&, Fit& cache, const juce::String& text, juce::Rectangle<float> area,
                     int maxLines, float maxFont, float minFont, bool bold, juce::Colour);

    void paintHeader (juce::Graphics&, juce::Rectangle<float>, float unit, const PrompterScene&, const Song&);
    void paintTimeline (juce::Graphics&, juce::Rectangle<float>, float unit, const PrompterScene&, const Song&);
    void paintLyrics (juce::Graphics&, juce::Rectangle<float>, float unit, const PrompterScene&);
    void paintStructureOnly (juce::Graphics&, juce::Rectangle<float>, float unit, const PrompterScene&, const Song&);
    // Previous (small, dim) - current (large, bass note under it) - next (with
    // "in 2 beats"). `big`: chords are the main thing on screen (lyrics off).
    void paintChords (juce::Graphics&, juce::Rectangle<float>, float unit, const PrompterScene&, bool big);

    Fit currentFit, nextFit, sectionFit;
};

}  // namespace boothcue
