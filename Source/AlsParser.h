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
// All times are in beats (quarter notes), the same unit as the host's PPQ.

struct AlsSection
{
    std::string name;
    double start = 0.0;   // beats
    double end   = 0.0;   // beats
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
};

// Parses the set at `path`. The structure track is the first MIDI track whose
// name equals `trackName` (case-insensitive, trimmed); if there is none, the
// 2nd track of the set is used. A file that isn't gzipped is read as plain XML.
AlsResult parseAlsFile (const std::string& path, const std::string& trackName = "parts");

// The same, from XML that is already in memory (tests).
AlsResult parseAlsXml (const std::string& xml, const std::string& trackName = "parts");

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
