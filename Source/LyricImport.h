#pragma once

#include "Song.h"

#include <string>
#include <vector>

namespace boothcue
{

// Plain-text lyrics with section headers:
//
//     [Verse 1]
//     First line
//     Second line
//
//     [Chorus]
//     ...
//
// A line that is just "[...]" starts a block; blank lines are ignored. Lines
// before the first header land in a block with an empty header, which links to
// no section. Plain C++, no JUCE.

std::vector<LyricBlock> parseLyrics (const std::string& text);

// The inverse: the text the blocks came from (timings are not part of it).
std::string lyricsToText (const std::vector<LyricBlock>&);

// After editing the text: lines that are unchanged keep the timing they had in
// `previous`. Blocks pair up by header and by position among the blocks with
// that header; inside a block, lines pair up by text, in order (longest common
// subsequence), so inserting or removing a line doesn't lose the other taps.
void keepTimings (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited);

}  // namespace boothcue
