#pragma once

#include "Song.h"

#include <string>
#include <vector>

namespace boothcue
{

// Lyrics as plain text, the way they get pasted from a document:
//
//     Papa die gaat naar de club          <- a title: skipped
//     Verse: ||: Am | | | Dm E7 :||       <- a chord chart: kept aside, not sung
//
//     verse 1                             <- a header: [Verse 1], "verse 1",
//     Papa heeft het voor elkaar,            "Refrein:", "Brug: first line…"
//     (Hij is)                            <- a short pickup in parentheses is
//     Best tevreden samen                    joined to the line it leads into
//
//     chorus
//     ...
//     Zet uw winkelwagen aan de kant! x 2  <- sung twice: two lines
//     @13.2.3 Hold on, hold on            <- a start given as bar.beat.16th
//                                            (BoothCue's export writes these)
//
//     Refrein                             <- a header used before, on its own:
//                                            "the chorus again", no new lines
//     30 dagen garantie                   <- a stanza that repeats an earlier
//     ...                                    one (a chorus written out again,
//                                            without its header) gets its header
//
// Stanzas (separated by blank lines) without a header belong to the block above
// them, except right after a repeat marker, where they start a block without a
// header (not linked to any section; the editor says so). Lines before the
// first header land in such a block too. Plain C++, no JUCE.

// What the import noticed, for the editor to report.
struct LyricImportReport
{
    std::string title;                      // the title line that was skipped, if any
    std::vector<std::string> chordCharts;   // chord-chart lines, as written (for the chord display later)
    int  pickupsJoined   = 0;               // "(Hij is)" + next line
    int  linesRepeated   = 0;               // extra lines from "x2" markers
    int  repeatMarkers   = 0;               // bare headers meaning "this part again"
    int  stanzasMatched  = 0;               // header-less stanzas recognised as a repeat
    bool anyHeader       = false;           // false: nothing can link to the song's sections
    int  timedLines      = 0;               // lines with an "@bar.beat" start
};

std::vector<LyricBlock> parseLyrics (const std::string& text, LyricImportReport* report = nullptr);

// The inverse: the text the blocks came from (timings are not part of it).
std::string lyricsToText (const std::vector<LyricBlock>&);

// For export: the same, with "@bar.beat.16th " in front of every tapped line
// (its start in the first part that sings it). parseLyrics reads it back.
std::string lyricsToTimedText (const Song&);

// After editing the text: lines that are unchanged keep the timing they had in
// `previous`. Blocks pair up by header and by position among the blocks with
// that header; inside a block, lines pair up by text, in order (longest common
// subsequence), so inserting or removing a line doesn't lose the other taps.
void keepTimings (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited);

}  // namespace boothcue
