#pragma once

#include <string>
#include <vector>

namespace boothcue
{

// Chord detection from the MIDI notes in the set, for keyboard and guitar
// players: which chord is sounding, and the bass note under it.
//
// Keys parts hide the harmony in embellishments, so the notes are cleaned and
// weighed before anything is matched (see TODO.md, "Next: chords"):
//   1. per track a role: chords (keys, pads, guitar), bass, melody (weighs
//      little), or ignored; "auto" decides from the notes
//   2. embellishments filtered: grace notes, short off-beat notes, passing and
//      neighbour notes; an appoggiatura or suspension that resolves by step
//      gives its weight to the note it resolves to
//   3. per beat slice a weight per pitch class: time sounding x metric weight
//      x register x track role x velocity; chord notes carry on a little after
//      they stop (as with the sustain pedal), so arpeggios add up
//   4. the bass note per slice, from the bass track (or the lowest note)
//   5. every root x quality scored against the weights, with the bass note as
//      a strong hint for the root and the song's key as a weak one
//   6. Viterbi over the slices: changing chord costs, less on a bar line
// Plain C++, no JUCE, no clock. Times are in beats (quarter notes).

struct ChordNote
{
    double start = 0.0, length = 0.0;
    int pitch = 60;       // MIDI note number
    int velocity = 100;
    int track = 0;        // index into ChordInput::tracks
};

enum class TrackRole { automatic, chords, bass, melody, ignore };

struct ChordTrack
{
    std::string name;
    TrackRole role = TrackRole::automatic;
};

struct ChordInput
{
    std::vector<ChordTrack> tracks;
    std::vector<ChordNote> notes;
};

enum class Quality
{
    none,   // no chord
    major, minor, dim, aug, sus2, sus4,
    dom7, maj7, min7, halfDim7, dim7, six, min6, add9, minAdd9, power
};

struct Chord
{
    int root = -1;               // pitch class 0..11, -1 = no chord
    Quality quality = Quality::none;
    int bass = -1;               // pitch class of the lowest note, -1 = unknown

    bool isChord() const { return root >= 0 && quality != Quality::none; }
    bool operator== (const Chord& o) const { return root == o.root && quality == o.quality && bass == o.bass; }
    bool operator!= (const Chord& o) const { return ! (*this == o); }
};

struct ChordSpan
{
    double start = 0.0, end = 0.0;
    Chord chord;
    double confidence = 0.0;     // 0..1: how clearly it won over the runner-up
};

struct Key
{
    int tonic = 0;               // pitch class
    bool minor = false;
};

struct ChordSettings
{
    double sliceBeats = 1.0;     // the time grid chords are judged on
    double barBeats   = 4.0;     // bar length, for the metric weights and the change cost
    double carryBeats = 0.75;    // chord-track notes count this much longer (pedal-like)
    bool   filterEmbellishments = true;
    double changeCost = 0.35;    // cost of a chord change mid-bar...
    double halfBarChangeCost = 0.22;
    double barChangeCost = 0.12; // ...on a half bar, on a bar line
    // Notes below this are left out (MIDI 33 = "A0" in Live's note names, the A
    // under Live's "E1"): sound selectors / keyswitches in bass instruments
    // live down there (e.g. MODO BASS), real bass notes don't.
    int lowestPitch = 33;
    double sameRootChangeCost = 0.30;   // extra for a change that keeps the root (Bbm -> Bbsus4 -> Bbm):
                                        // a line moving on top, not a new chord
    bool   explain = false;          // fill ChordResult::slices (for tuning)
};

// Per slice, with settings.explain: what the detector saw and weighed.
struct ChordSliceInfo
{
    double start = 0.0;
    double weight[12] {};            // per pitch class
    int bass = -1;
    struct Candidate { Chord chord; double score; };
    std::vector<Candidate> top;      // the three best chords
};

struct ChordResult
{
    std::vector<ChordSpan> spans;    // sorted, contiguous where there is harmony
    Key key;
    std::vector<TrackRole> roles;    // the role each track ended up with
    std::vector<ChordSliceInfo> slices;   // only with settings.explain
};

ChordResult detectChords (const ChordInput&, double songEnd, const ChordSettings& = {});

// A track's role from its name: "Bass", "Synth bass" -> bass; "Lead vox",
// "Melodie", "Topline" -> melody; "Drums", "Perc", "Click" and the song's own
// helper tracks ("parts", "length") -> ignore; anything else: decided from
// its notes. Case-insensitive.
TrackRole roleFromName (const std::string& trackName);

// "Am", "Bb/F", "F#m7b5", "Ebmaj7", "E7", "C5", "N.C."; flats or sharps from the key.
std::string chordName (const Chord&, const Key& = {});
std::string keyName (const Key&);

// The inverse, for chord charts written by hand: "Dm7", "C/G", "Bb/F",
// "E(7b13)" (-> E7: tensions beyond the 7th are not tracked), "Fmsus2"
// (-> Fm(add9)), "Esus4", "-" (no chord). false if it isn't a chord name.
bool parseChordName (const std::string&, Chord&);

// The notes of a chord, for tests and voicing: root, then the chord tones above.
std::vector<int> chordIntervals (Quality);

}  // namespace boothcue
