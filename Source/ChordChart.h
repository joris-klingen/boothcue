#pragma once

#include "Chords.h"
#include "Song.h"

#include <string>
#include <vector>

namespace boothcue
{

// The chords as text: BoothCue's chords "database" is edited, imported and
// exported as a chart in the band's own notation. Plain C++, no JUCE.
//
//     # comments
//     [verse]                          a part, in song order: the next part of
//     | Bbm | Eb/Bb Bbm | Ab | Ab |     that name gets these bars, from its start
//     | Eb . . Db | - | % | Bbm |
//
//     Verse/Chorus: ||: Am | | | Dm E(7b13) :||
//                                      a progression per kind of part: repeated
//                                      to fill every part of those kinds that has
//                                      no [block] of its own (the band's charts)
//
// In a bar: chords share it evenly ("Dm E7" = half each); "." = one more slot
// for the chord before ("Eb . . Db" = 3 beats + 1 in 4/4); "-" = no chord; an
// empty bar = the chord goes on; "%" = the bar before again; "||: ... :||" =
// twice; "(Bb)" = an alternative, left out; "x2" after a line = the line twice.

struct ChartImport
{
    std::vector<ChordSpan> chords;          // sorted, adjacent equal chords merged
    std::vector<std::string> unknown;       // tokens that aren't chords, as written
    std::vector<std::string> unmatched;     // [blocks] / labels with no part to go to
    int partsFilled = 0;                    // parts that got chords
};

// The chart for the chords of a song: a [block] per part, 4 bars per line, the
// chord per beat (slots finer than a beat are rounded to beats).
std::string chordsToChart (const std::vector<Section>&, const std::vector<ChordSpan>&, const Key&, TimeSig);

ChartImport chartToChords (const std::string& chart, const std::vector<Section>&, TimeSig);

}  // namespace boothcue
