#pragma once

#include "AlsParser.h"
#include "Chords.h"

#include <string>
#include <vector>

namespace boothcue
{

// The chords of a parsed Live set (parseAlsFile with readNotes): every MIDI
// track becomes detector input, with its role from its name (roleFromName),
// except muted tracks, tracks with a Drum Rack and the structure track itself.
// Plain C++, no JUCE.

struct SetChords
{
    ChordResult result;
    std::vector<std::string> chordTracks, bassTracks, melodyTracks;   // what was used, for the editor
    bool anyNotes = false;
};

SetChords chordsFromSet (const AlsResult&, double barBeats = 4.0);

}  // namespace boothcue
