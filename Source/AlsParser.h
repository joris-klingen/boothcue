#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace boothcue
{

// Reads the song structure out of an Ableton Live set (.als = gzipped XML).
//
// Plain C++ (no JUCE): zlib inflates the file in chunks straight into a small
// streaming XML scanner, so a set that is hundreds of MB uncompressed never
// sits in memory, and scanning stops at </Tracks>.
//
// The XML paths follow bbb_code (bbb/als.py) and ALSmuse:
//   tracks  : Ableton/LiveSet/Tracks/*            (document order = track order)
//   name    : <track>/Name/UserName, else <track>/Name/EffectiveName
//   clips   : <track>/DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events/MidiClip
//   start   : MidiClip@Time        end : MidiClip/CurrentEnd@Value
//   name    : MidiClip/Name@Value
// and, with readNotes, per MIDI track:
//   notes   : MidiClip/Notes/KeyTracks/KeyTrack/{Notes/MidiNoteEvent@Time,Duration,
//             Velocity,IsEnabled ; MidiKey@Value}   (times in clip content beats)
//   timing  : MidiClip/Loop/{LoopStart,LoopEnd,StartRelative,LoopOn}: playback
//             starts at the start marker (LoopStart + StartRelative); a looped
//             clip repeats [LoopStart, LoopEnd) until CurrentEnd, an unlooped one
//             stops at LoopEnd (its end marker)
//   skipped : MidiClip/Disabled true (deactivated clip), IsEnabled false notes
//   track   : DeviceChain/Mixer/Speaker/Manual false = muted; a DrumGroupDevice
//             anywhere in it = drums
// All times are in beats (quarter notes), the same unit as the host's PPQ.

struct AlsSection
{
    std::string name;
    double start = 0.0;   // beats
    double end   = 0.0;   // beats
};

// A MIDI note placed in the arrangement (clip loops unrolled, start marker
// and end applied).
struct AlsNote
{
    double start = 0.0, length = 0.0;   // beats
    int pitch = 60;
    int velocity = 100;
};

// A MIDI track's arrangement notes, for chord detection.
struct AlsMidiTrack
{
    std::string name;
    int index = -1;        // position in the set
    bool muted = false;    // the track's Speaker (activator) is off
    bool drums = false;    // a Drum Rack is on it
    std::vector<AlsNote> notes;   // sorted by start
};

struct AlsResult
{
    bool ok = false;
    std::string error;                  // set when !ok

    std::vector<AlsSection> sections;   // sorted by start
    std::string trackName;              // the track the sections came from
    int  trackIndex   = -1;             // 0-based position in the set
    bool usedFallback = false;          // true: no track with the wanted name, took the 2nd track
    int  trackCount   = 0;              // tracks seen before scanning stopped

    // With readNotes: every MIDI track with its notes (the whole track list is
    // read then, not only up to the structure track).
    std::vector<AlsMidiTrack> midiTracks;
};

// Parses the set at `path`. The structure track is the first MIDI track whose
// name equals `trackName` (case-insensitive, trimmed); if there is none, the
// 2nd track of the set is used. A file that isn't gzipped is read as plain XML.
AlsResult parseAlsFile (const std::string& path, const std::string& trackName = "parts", bool readNotes = false);

// The same, from XML that is already in memory (tests).
AlsResult parseAlsXml (const std::string& xml, const std::string& trackName = "parts", bool readNotes = false);

// ---- streaming XML scanner --------------------------------------------------
// Just enough XML for Live sets: elements and attributes. Text, comments,
// processing instructions and CDATA are skipped. Feed it chunks of any size.

class XmlScanner
{
public:
    using Attributes = std::vector<std::pair<std::string, std::string>>;

    struct Handler
    {
        virtual ~Handler() = default;
        virtual void startElement (const std::string& name, const Attributes&) = 0;
        virtual void endElement (const std::string& name) = 0;
        virtual bool done() const { return false; }   // true: stop scanning
    };

    explicit XmlScanner (Handler& h) : handler (h) {}

    // Returns false on malformed input.
    bool feed (const char* data, std::size_t size);
    bool finished() const { return handler.done(); }

private:
    bool parseTag (const char* begin, const char* end);

    Handler& handler;
    std::string pending;   // an incomplete tag carried over to the next chunk
    XmlScanner::Attributes attrs;
    std::string tagName;
};

}  // namespace boothcue
