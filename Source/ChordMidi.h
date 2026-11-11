#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Chords.h"
#include "Song.h"

namespace boothcue
{

// The chords as a standard MIDI file, to drag into Live as a chord track (or
// to save), and back.
//
// Out: one track, "<name> chords": per chord its notes in a close voicing
// around middle C plus the bass note in octave 2 (so slash chords show), and
// the chord name as a text event at its start (other tools show those; Live
// shows the notes). Time 0 = bar 1: drop it at bar 1 in Live. Tempo and time
// signature are written too.
//
// In: a file with chord-name text events (BoothCue's own export, and other
// tools that write them) gives exactly those chords; any other MIDI file goes
// through the chord detector.

juce::MidiFile chordsToMidi (const std::vector<ChordSpan>&, const Key&, TimeSig, double bpm, const juce::String& trackName);
bool writeChordsMidi (const juce::File&, const std::vector<ChordSpan>&, const Key&, TimeSig, double bpm,
                      const juce::String& trackName);

struct MidiChordImport
{
    bool ok = false;
    juce::String error;
    std::vector<ChordSpan> chords;
    bool fromNames = false;    // read from chord-name text events (else detected from the notes)
    Key key;
};

MidiChordImport chordsFromMidiFile (const juce::File&, TimeSig);

// The notes BoothCue writes for a chord: the voicing, then the bass note.
std::vector<int> chordVoicing (const Chord&);

}  // namespace boothcue
