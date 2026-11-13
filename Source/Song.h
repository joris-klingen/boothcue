#pragma once

#include "Chords.h"

#include <string>
#include <vector>

namespace boothcue
{

// The song as the singer sees it: sections (from the Live set), lyric blocks
// (from the imported text) and the cues that tie each lyric line to a song
// position. Plain C++, no JUCE, no clock: every query takes the position.
//
// All positions are in beats (quarter notes) = the host's PPQ.

struct Section
{
    std::string name;
    double start = 0.0;
    double end   = 0.0;
    int color = -1;      // the clip colour in Live (palette index 0..69), -1 = unknown
    double length() const { return end - start; }
};

struct TimeSig
{
    int num = 4, den = 4;
    double beatLength() const { return 4.0 / (den > 0 ? den : 4); }
    double barLength()  const { return (num > 0 ? num : 4) * beatLength(); }
};

// Bar 1 beat 1 = beat 0. "13.2.3" in 4/4 = 48 + 1 + 0.5.
double positionToPpq (int bar, int beat, int sixteenth, TimeSig);

struct LyricLine
{
    std::string text;
    // Beats from the start of the section the line belongs to. Negative =
    // not tapped yet: such lines are spread evenly between their neighbours.
    double offset = -1.0;
    // A start given as a song position ("@13.2.3" in imported text): turned
    // into the offset once the block is linked to a section (Song::rebuild).
    bool hasAt = false;
    int atBar = 1, atBeat = 1, atSixteenth = 1;
    // The first line after a blank line in the text: the paragraphs of a
    // part (the display can show the lyrics a paragraph at a time).
    bool paraStart = false;
    bool timed() const { return offset >= 0.0; }
};

// One [Header] of the lyric text and the lines under it.
struct LyricBlock
{
    std::string header;
    std::vector<LyricLine> lines;
};

// A lyric line placed on the timeline. One block can produce cues in several
// sections (a [Chorus] block is sung in every Chorus).
struct Cue
{
    double ppq = 0.0;
    int section = -1;
    int block   = -1;
    int line    = -1;
    bool timed  = false;
};

class Song
{
public:
    void setSections (std::vector<Section>);
    void setBlocks (std::vector<LyricBlock>);
    // Bar length in beats for spreading untimed lines (they land on bars, or
    // half / quarter bars when there are more lines than bars). 0 = no grid.
    void setBarLength (double beats);
    // The time signature: the bar grid, and how "@bar.beat" positions read.
    void setTimeSig (TimeSig);
    TimeSig timeSig() const { return sig_; }
    double barLength() const { return barLength_; }

    const std::vector<Section>&    sections() const { return sections_; }
    const std::vector<LyricBlock>& blocks()   const { return blocks_; }
    const std::vector<Cue>&        cues()     const { return cues_; }

    double start() const { return sections_.empty() ? 0.0 : sections_.front().start; }
    double end() const;

    // ---- chords (detected from the set's MIDI, or written by hand) -----------
    void setChords (std::vector<ChordSpan>, Key);
    const std::vector<ChordSpan>& chords() const { return chords_; }
    Key key() const { return key_; }
    int chordAt (double ppq) const;            // the chord sounding; -1 in a gap
    int previousChordBefore (double ppq) const;   // the one before chordAt (or before ppq)
    int nextChordAfter (double ppq) const;     // the first one starting after ppq

    // ---- lookups (all return -1 for "none") --------------------------------
    int sectionAt (double ppq) const;
    int nextSectionAfter (double ppq) const;       // first section starting after ppq
    int blockForSection (int section) const;       // the lyric block sung in that section
    int firstSectionForBlock (int block) const;
    int cueAt (double ppq) const;                  // the line being sung; none once its section is over
    int nextCueAfter (double ppq) const;

    // Where a line sits inside its section (tapped, or the even spread),
    // measured against the first section the block is sung in. -1 if unlinked.
    double resolvedOffset (int block, int line) const;

    // ---- tap-along -----------------------------------------------------------
    // Stamps the line at song position ppq. Fails (false) when the section at
    // ppq isn't one this block is sung in. Neighbours whose taps would now be
    // out of order go back to untimed.
    bool stamp (int block, int line, double ppq);
    // Moves a line by delta beats, staying between its tapped neighbours.
    bool nudge (int block, int line, double delta);
    void clearTiming (int block, int line);
    void clearAllTimings();

private:
    void rebuild();

    std::vector<Section>    sections_;
    std::vector<LyricBlock> blocks_;
    double                  barLength_ = 0.0;
    TimeSig                 sig_;
    std::vector<int>        sectionBlock_;   // per section: block index or -1
    std::vector<Cue>        cues_;           // sorted by ppq
    std::vector<ChordSpan>  chords_;         // sorted by start
    Key                     key_;
};

// Offsets (beats from section start) for every line of a block in a section of
// the given length: tapped lines keep their offset, the others are spread
// evenly between their tapped neighbours / the section edges.
//
// With a bar length, the spread lines are snapped to the song's bar grid
// (counted from beat 0, so a section that starts off the bar still lands its
// later lines on bar lines; its first line stays on the section start): bars
// if that keeps every line apart and inside its gap, else half bars, else
// quarter bars, else unsnapped.
std::vector<double> spreadOffsets (const LyricBlock&, double sectionLength,
                                   double sectionStart = 0.0, double barLength = 0.0);

// What kind of part a section is, from its name (Dutch and short names too:
// "Refrein 2" is a chorus, "Pre" a pre-chorus, "Brug" a bridge, "Slot" an
// outro). Used for the colours on the display.
enum class SectionKind { intro, verse, prechorus, chorus, bridge, outro, other };
SectionKind sectionKind (const std::string& sectionName);

// "Verse 1", "verse-1" and " VERSE 1 " all give "verse1"; Dutch and short
// section words map to one name ("Refrein" -> "chorus", "Brug 2" -> "bridge2",
// "Pre" / "Pre-Chorus" / "Voorrefrein" -> "prechorus", "Couplet" -> "verse").
std::string nameKey (const std::string&);

// ---- tap-along ---------------------------------------------------------------

// The line the next tap stamps. Selecting a line in the editor points it there;
// each tap moves it on to the next line.
struct TapCursor
{
    int block = -1, line = -1;
    int section = -1;   // the section the last tap landed in (-1: none yet)
};

struct TapOutcome
{
    bool ok = false;
    int block = -1, line = -1, section = -1;
    std::string error;   // why nothing was stamped
};

// A tap at song position ppq: stamps the cursor's line, or, when the playhead
// is in a section that sings another block, that block's first line; when the
// block was finished in an earlier section (the 2nd chorus), it starts over.
TapOutcome tapAt (Song&, TapCursor&, double ppq);

// ---- bars, beats and the count-in ---------------------------------------------

struct BarBeat
{
    int bar = 1, beat = 1;   // 1-based, as Live shows them
};

// `lastBarStart` is the host's ppqPositionOfLastBarStart when it gives one
// (pass hasLastBarStart = false otherwise). The time signature is assumed
// constant from the start of the song.
BarBeat barBeatAt (double ppq, TimeSig, bool hasLastBarStart = false, double lastBarStart = 0.0);

// Live's bar.beat.sixteenth, e.g. "13.2.3".
std::string formatPosition (double ppq, TimeSig);

struct CountSettings
{
    int    countInBars  = 2;     // bars announced before the final counted bar; 0 = no count-in
    double entryGapBars = 4.0;   // a line is a "vocal entry" when the previous line started this long before
    bool   enabled      = true;
};

struct Countdown
{
    enum class Kind { none, bars, beats };
    Kind kind = Kind::none;
    int value = 0;    // bars: "2 bars", "1 bar";  beats: 4, 3, 2, 1
    int cue   = -1;   // the entry being counted in
};

// "2 bars… 1… 4-3-2-1": bars are announced first, the last bar is counted in beats.
Countdown countdownAt (const Song&, double ppq, TimeSig, const CountSettings&);

}  // namespace boothcue
