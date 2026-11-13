// boothcue-chart — what BoothCue reads from a Live set, for other programs: the
// parts of the `parts` track and the chords detected from the set's MIDI, as
// the chord chart the Chords tab shows. The songbook (bovenburen_manager) runs
// it to offer "chords from the Live set", so there is one detector, this one.
//
//   boothcue-chart <set.als> [time signature, e.g. 6/8]
//
// Prints one JSON object: {"track", "fallback", "sig", "parts": [{"name",
// "start", "end", "color"}], "chart"}. start / end are in beats; color is
// Live's clip colour index (-1: none). Exit 1 with the reason on stderr when
// the set can't be read. Plain C++, no JUCE; the set is only read.

#include "AlsParser.h"
#include "ChordChart.h"
#include "SetChords.h"
#include "Song.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace boothcue;

namespace
{
    std::string quoted (const std::string& s)
    {
        std::string out = "\"";
        for (const char ch : s)
        {
            const auto c = static_cast<unsigned char> (ch);
            if (c == '"')        out += "\\\"";
            else if (c == '\\')  out += "\\\\";
            else if (c == '\n')  out += "\\n";
            else if (c == '\r')  out += "\\r";
            else if (c == '\t')  out += "\\t";
            else if (c < 0x20)
            {
                char buf[8];
                std::snprintf (buf, sizeof buf, "\\u%04x", c);
                out += buf;
            }
            else
                out += ch;   // UTF-8 as it is
        }
        return out + "\"";
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: boothcue-chart <set.als> [time signature, e.g. 6/8]\n");
        return 2;
    }

    TimeSig sig;
    if (argc >= 3)
    {
        int num = 0, den = 0;
        if (std::sscanf (argv[2], "%d/%d", &num, &den) == 2 && num >= 1 && num <= 32
            && (den == 1 || den == 2 || den == 4 || den == 8 || den == 16))
            sig = TimeSig { num, den };
    }

    const auto r = parseAlsFile (argv[1], "parts", true);
    if (! r.ok)
    {
        std::fprintf (stderr, "%s\n", r.error.c_str());
        return 1;
    }

    const auto detected = chordsFromSet (r, sig.barLength());
    std::vector<Section> sections;
    for (const auto& sec : r.sections)
        sections.push_back ({ sec.name, sec.start, sec.end, sec.color });
    const auto chart = chordsToChart (sections, detected.result.spans, detected.result.key, sig);

    std::string out = "{\"track\": " + quoted (r.trackName)
                    + ", \"fallback\": " + (r.usedFallback ? "true" : "false")
                    + ", \"sig\": " + quoted (std::to_string (sig.num) + "/" + std::to_string (sig.den))
                    + ", \"parts\": [";
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        char numbers[96];
        std::snprintf (numbers, sizeof numbers, ", \"start\": %.6f, \"end\": %.6f, \"color\": %d}",
                       sections[i].start, sections[i].end, sections[i].color);
        out += std::string (i > 0 ? ", " : "") + "{\"name\": " + quoted (sections[i].name) + numbers;
    }
    out += "], \"chart\": " + quoted (chart) + "}\n";
    std::fputs (out.c_str(), stdout);
    return 0;
}
