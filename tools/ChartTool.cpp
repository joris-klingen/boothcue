// boothcue-chart — what BoothCue reads from a Live set, for other programs: the
// parts of the `parts` track and the chords detected from the set's MIDI, as
// the chord chart the Chords tab shows. The songbook (bovenburen_manager) runs
// it to offer "chords from the Live set", so there is one detector, this one.
//
//   boothcue-chart <set.als> [time signature, e.g. 6/8] [--lay <chart.txt>]
//
// Prints one JSON object: {"track", "fallback", "sig", "parts": [{"name",
// "start", "end", "color", "bars"}], "chart"}. start / end are in beats; color
// is Live's clip colour index (-1: none); bars are the chords detected in each
// bar of the part ("Bbm", "Bbm Db", "-" for none).
//
// With --lay: a chord chart (the songbook's chords.txt) is laid over the
// set's parts the way the plugin does it, and "laid" tells the result:
// {"filled", "unmatched", "unknown", "parts": [{"name", "bars"}]}. The
// songbook's self-check compares that with what was detected. Exit 1 with the reason on stderr when
// the set can't be read. Plain C++, no JUCE; the set is only read.

#include "AlsParser.h"
#include "Chords.h"
#include "ChordChart.h"
#include "SetChords.h"
#include "Song.h"

#include <algorithm>
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

namespace
{
    // The chords in every bar of a part, as names: "Bbm", "Bbm Db", "-".
    std::string barsJson (const std::vector<ChordSpan>& spans, const Section& sec, const Key& key, TimeSig sig)
    {
        std::string out = "[";
        const double bar = sig.barLength();
        bool firstBar = true;
        for (double t = sec.start; t < sec.end - 1.0e-6; t += bar)
        {
            std::string names;
            std::string last;
            for (const auto& span : spans)
            {
                if (span.end <= t + 1.0e-6 || span.start >= std::min (t + bar, sec.end) - 1.0e-6)
                    continue;
                const auto name = chordName (span.chord, key);
                if (name != last)
                    names += (names.empty() ? "" : " ") + name;
                last = name;
            }
            out += std::string (firstBar ? "" : ", ") + quoted (names.empty() ? "-" : names);
            firstBar = false;
        }
        return out + "]";
    }

    std::string listJson (const std::vector<std::string>& items)
    {
        std::string out = "[";
        for (std::size_t i = 0; i < items.size(); ++i)
            out += std::string (i > 0 ? ", " : "") + quoted (items[i]);
        return out + "]";
    }
}

namespace
{
    std::string readFile (const char* path)
    {
        std::string text;
        if (auto* f = std::fopen (path, "rb"))
        {
            char buf[4096];
            for (std::size_t n; (n = std::fread (buf, 1, sizeof buf, f)) > 0 && text.size() < (1u << 20);)
                text.append (buf, n);
            std::fclose (f);
        }
        return text;
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
    const char* layPath = nullptr;
    for (int i = 2; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--lay")
            layPath = argv[i + 1];
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
        std::snprintf (numbers, sizeof numbers, ", \"start\": %.6f, \"end\": %.6f, \"color\": %d",
                       sections[i].start, sections[i].end, sections[i].color);
        out += std::string (i > 0 ? ", " : "") + "{\"name\": " + quoted (sections[i].name) + numbers
             + ", \"bars\": " + barsJson (detected.result.spans, sections[i], detected.result.key, sig) + "}";
    }
    out += "], \"chart\": " + quoted (chart);
    if (layPath != nullptr)
    {
        const auto laid = chartToChords (readFile (layPath), sections, sig);
        out += ", \"laid\": {\"filled\": " + std::to_string (laid.partsFilled)
             + ", \"unmatched\": " + listJson (laid.unmatched) + ", \"unknown\": " + listJson (laid.unknown) + ", \"parts\": [";
        for (std::size_t i = 0; i < sections.size(); ++i)
            out += std::string (i > 0 ? ", " : "") + "{\"name\": " + quoted (sections[i].name)
                 + ", \"bars\": " + barsJson (laid.chords, sections[i], detected.result.key, sig) + "}";
        out += "]}";
    }
    out += "}\n";
    std::fputs (out.c_str(), stdout);
    return 0;
}
