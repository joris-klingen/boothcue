#pragma once

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
    double length() const { return end - start; }
};

struct LyricLine
{
    std::string text;
    // Beats from the start of the section the line belongs to. Negative =
    // not tapped yet: such lines are spread evenly between their neighbours.
    double offset = -1.0;
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

    const std::vector<Section>&    sections() const { return sections_; }
    const std::vector<LyricBlock>& blocks()   const { return blocks_; }
    const std::vector<Cue>&        cues()     const { return cues_; }

    double start() const { return sections_.empty() ? 0.0 : sections_.front().start; }
    double end() const;

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
    std::vector<int>        sectionBlock_;   // per section: block index or -1
    std::vector<Cue>        cues_;           // sorted by ppq
};

// Offsets (beats from section start) for every line of a block in a section of
// the given length: tapped lines keep their offset, the others are spread
// evenly between their tapped neighbours / the section edges.
std::vector<double> spreadOffsets (const LyricBlock&, double sectionLength);

// "Verse 1", "verse-1" and " VERSE 1 " all give "verse1".
std::string nameKey (const std::string&);

// ---- bars, beats and the count-in ---------------------------------------------

struct TimeSig
{
    int num = 4, den = 4;
    double beatLength() const { return 4.0 / (den > 0 ? den : 4); }
    double barLength()  const { return (num > 0 ? num : 4) * beatLength(); }
};

struct BarBeat
{
    int bar = 1, beat = 1;   // 1-based, as Live shows them
};

// `lastBarStart` is the host's ppqPositionOfLastBarStart when it gives one
// (pass hasLastBarStart = false otherwise). The time signature is assumed
// constant from the start of the song.
BarBeat barBeatAt (double ppq, TimeSig, bool hasLastBarStart = false, double lastBarStart = 0.0);

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
