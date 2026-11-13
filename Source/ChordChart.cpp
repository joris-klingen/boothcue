#include "ChordChart.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace boothcue
{

namespace
{
    constexpr double kEps = 1.0e-6;

    std::string trim (const std::string& s)
    {
        const auto a = s.find_first_not_of (" \t\r\n");
        if (a == std::string::npos)
            return {};
        return s.substr (a, s.find_last_not_of (" \t\r\n") - a + 1);
    }

    // ---- writing -----------------------------------------------------------------------

    // The chord sounding at t (by index), -1 if none.
    int chordIndexAt (const std::vector<ChordSpan>& chords, double t)
    {
        for (std::size_t i = 0; i < chords.size(); ++i)
            if (t >= chords[i].start - kEps && t < chords[i].end - kEps)
                return static_cast<int> (i);
        return -1;
    }

    std::string barText (const std::vector<ChordSpan>& chords, const Key& key, double barStart, TimeSig sig)
    {
        const int slots = std::max (1, sig.num);
        std::vector<std::string> perBeat;
        for (int b = 0; b < slots; ++b)
        {
            const int i = chordIndexAt (chords, barStart + (b + 0.5) * sig.beatLength());
            perBeat.push_back (i < 0 ? "-" : chordName (chords[static_cast<std::size_t> (i)].chord, key));
        }
        // One chord all bar; or two halves; or chord + "." per extra beat.
        if (std::all_of (perBeat.begin(), perBeat.end(), [&] (const std::string& c) { return c == perBeat[0]; }))
            return perBeat[0];
        if (slots % 2 == 0)
        {
            const auto half = static_cast<std::size_t> (slots / 2);
            bool halves = true;
            for (std::size_t b = 0; b < perBeat.size(); ++b)
                halves = halves && perBeat[b] == perBeat[b < half ? 0 : half];
            if (halves)
                return perBeat[0] + " " + perBeat[half];
        }
        std::string out;
        for (std::size_t b = 0; b < perBeat.size(); ++b)
            out += (b == 0 ? "" : " ") + (b > 0 && perBeat[b] == perBeat[b - 1] ? std::string (".") : perBeat[b]);
        return out;
    }

    // ---- reading -----------------------------------------------------------------------

    struct Change
    {
        double at;
        int order;     // 0: a part ends, 1: a chord (or "-") starts; ends sort first
        Chord chord;
    };

    struct Slot
    {
        bool set = false;      // false: the chord before goes on
        Chord chord;           // ! isChord(): no chord ("-")
        int length = 1;        // in slots of its bar
    };
    struct Bar
    {
        std::vector<Slot> slots;   // empty: the chord before goes on all bar
    };

    // A run of bars ("| A | B C | % |", "||: A | B :||", "x2").
    std::vector<Bar> parseBars (const std::string& text, std::vector<std::string>& unknown)
    {
        std::string s = text;
        int lineRepeat = 1;
        {
            // "x2" / "2x" / "(2x)" at the end: the whole line again.
            const auto t = trim (s);
            std::size_t p = t.size();
            while (p > 0 && (std::isalnum (static_cast<unsigned char> (t[p - 1])) || t[p - 1] == '(' || t[p - 1] == ')'))
                --p;
            std::string tail = t.substr (p);
            tail.erase (std::remove_if (tail.begin(), tail.end(), [] (char c) { return c == '(' || c == ')'; }), tail.end());
            if (tail.size() >= 2 && (tail[0] == 'x' || tail[0] == 'X') && std::isdigit (static_cast<unsigned char> (tail[1])))
                lineRepeat = std::clamp (std::atoi (tail.c_str() + 1), 1, 16);
            else if (tail.size() >= 2 && std::isdigit (static_cast<unsigned char> (tail[0])) && (tail.back() == 'x' || tail.back() == 'X'))
                lineRepeat = std::clamp (std::atoi (tail.c_str()), 1, 16);
            if (lineRepeat > 1)
                s = t.substr (0, p);
        }

        // Cells between bar lines; repeat signs kept as markers.
        std::vector<Bar> bars;
        std::vector<Bar>* out = &bars;
        std::vector<Bar> repeatBlock;
        bool inRepeat = false;
        std::string cell;
        auto flushCell = [&] (bool atBarLine)
        {
            const auto c = trim (cell);
            cell.clear();
            if (! atBarLine && c.empty())
                return;
            if (c.empty())
            {
                out->push_back (Bar());
                return;
            }
            if (c == "%")
            {
                out->push_back (out->empty() ? (bars.empty() ? Bar() : bars.back()) : out->back());
                return;
            }
            Bar bar;
            std::istringstream words (c);
            std::string w;
            while (words >> w)
            {
                if (w.size() >= 2 && w.front() == '(' && w.back() == ')')
                    continue;   // an alternative
                if (w == ".")
                {
                    if (! bar.slots.empty())
                        ++bar.slots.back().length;
                    continue;
                }
                Slot slot;
                slot.set = true;
                if (! parseChordName (w, slot.chord))
                {
                    unknown.push_back (w);
                    continue;
                }
                bar.slots.push_back (slot);
            }
            out->push_back (bar);
        };

        // Walk the text: "|" ends a bar, "||:" opens a repeat, ":||" closes it.
        std::size_t i = 0;
        bool started = false;
        while (i < s.size())
        {
            if (s.compare (i, 3, "||:") == 0 || s.compare (i, 2, "|:") == 0)
            {
                if (started) flushCell (false);
                started = true;
                inRepeat = true;
                repeatBlock.clear();
                out = &repeatBlock;
                i += s.compare (i, 3, "||:") == 0 ? 3 : 2;
                continue;
            }
            if (s.compare (i, 3, ":||") == 0 || s.compare (i, 2, ":|") == 0)
            {
                flushCell (true);
                if (inRepeat)
                {
                    for (int r = 0; r < 2; ++r)
                        bars.insert (bars.end(), repeatBlock.begin(), repeatBlock.end());
                    inRepeat = false;
                    out = &bars;
                }
                i += s.compare (i, 3, ":||") == 0 ? 3 : 2;
                started = false;
                continue;
            }
            if (s[i] == '|')
            {
                if (started)
                    flushCell (true);
                started = true;
                i += s.compare (i, 2, "||") == 0 ? 2 : 1;
                continue;
            }
            cell += s[i++];
        }
        if (! trim (cell).empty())
            flushCell (false);
        if (inRepeat)   // an unclosed repeat: just once
            bars.insert (bars.end(), repeatBlock.begin(), repeatBlock.end());

        std::vector<Bar> result;
        for (int r = 0; r < lineRepeat; ++r)
            result.insert (result.end(), bars.begin(), bars.end());
        return result;
    }

    // Every line is read on its own ("x2" belongs to its line; line breaks add no bars).
    std::vector<Bar> parseLines (const std::vector<std::string>& lines, std::vector<std::string>& unknown)
    {
        std::vector<Bar> bars;
        for (const auto& l : lines)
        {
            const auto more = parseBars (l, unknown);
            bars.insert (bars.end(), more.begin(), more.end());
        }
        return bars;
    }

    // Lays bars out from `start`, until `end` (repeating them if `loop`).
    // Writes per-beat chords into `grid` (index = beat from song start).
    void place (const std::vector<Bar>& bars, double start, double end, bool loop, TimeSig sig,
                std::vector<Change>& changes)
    {
        if (bars.empty())
            return;
        const double barLen = sig.barLength();
        double t = start;
        for (std::size_t n = 0; t < end - kEps; ++n)
        {
            if (n >= bars.size() && ! loop)
                break;
            const auto& bar = bars[n % bars.size()];
            int total = 0;
            for (const auto& slot : bar.slots)
                total += slot.length;
            double at = t;
            for (const auto& slot : bar.slots)
            {
                if (at < end - kEps)
                    changes.push_back ({ at, 1, slot.chord });
                at += barLen * slot.length / std::max (1, total);
            }
            t += barLen;
        }
        // The part's chords end with the part (or after the last bar written,
        // when the bars are not repeated).
        changes.push_back ({ std::min (t, end), 0, Chord() });
    }

    bool kindsMatch (const std::string& label, const Section& section)
    {
        const auto k = sectionKind (label);
        if (k != SectionKind::other)
            return sectionKind (section.name) == k;
        return nameKey (label) == nameKey (section.name);
    }
}

std::string chordsToChart (const std::vector<Section>& sections, const std::vector<ChordSpan>& chords, const Key& key,
                           TimeSig sig)
{
    std::string out = "# BoothCue chords - key " + keyName (key) + ", " + std::to_string (sig.num) + "/"
                      + std::to_string (sig.den) + "\n"
                      "# one |...| per bar  . = one more beat  - = no chord\n";
    const double barLen = sig.barLength();
    for (const auto& sec : sections)
    {
        out += "\n[" + sec.name + "]\n";
        int n = 0;
        for (double t = sec.start; t < sec.end - kEps; t += barLen, ++n)
        {
            out += (n % 4 == 0 ? "| " : " ") + barText (chords, key, t, sig) + " |";
            if (n % 4 == 3)
                out += "\n";
        }
        if (n % 4 != 0)
            out += "\n";
    }
    return out;
}

ChartImport chartToChords (const std::string& chart, const std::vector<Section>& sections, TimeSig sig)
{
    ChartImport result;
    std::vector<bool> assigned (sections.size(), false);

    // Pass 1: read blocks and label lines.
    struct Block { std::string name; bool label = false; std::vector<std::string> names; std::vector<std::string> lines; };
    std::vector<Block> blocks;
    std::istringstream lines (chart);
    std::string line;
    while (std::getline (lines, line))
    {
        const auto t = trim (line);
        if (t.empty() || t[0] == '#')
            continue;
        if (t.front() == '[' && t.back() == ']')
        {
            blocks.push_back ({ trim (t.substr (1, t.size() - 2)), false, {}, {} });
            continue;
        }
        const auto bar = t.find ('|');
        const auto colon = t.find (':');
        if (colon != std::string::npos && (bar == std::string::npos || colon < bar) && colon + 1 < t.size() + 1
            && ! (bar != std::string::npos && colon + 1 == bar))
        {
            // "Verse/Chorus: || Am | ..." (a label; ":" right before "|" is part of "||:" only after a bar)
            Block b;
            b.label = true;
            std::string names = t.substr (0, colon);
            names.erase (std::remove_if (names.begin(), names.end(), [] (char c) { return c == '(' || c == ')'; }), names.end());
            std::istringstream parts (names);
            std::string part;
            while (std::getline (parts, part, '/'))
                if (! trim (part).empty())
                    b.names.push_back (trim (part));
            b.name = names;
            b.lines.push_back (t.substr (colon + 1));
            blocks.push_back (b);
            continue;
        }
        if (blocks.empty())
            blocks.push_back ({ "", false, {}, {} });   // bars before any header: the first part
        blocks.back().lines.push_back (t);
    }

    // Pass 2: [blocks] go to parts in song order; then a part without a block
    // takes the one written for its name; labels fill the rest by kind.
    //
    // A chart is written as a person writes one (and as the songbook shows
    // it): "[verse]" once counts for every verse, "[verse 3]" is the third
    // verse where it differs, and the bars of a block go round for as long as
    // its part lasts.
    auto base = [] (const std::string& name)        // "verse3" -> "verse"
    {
        auto key = nameKey (name);
        while (! key.empty() && std::isdigit (static_cast<unsigned char> (key.back())))
            key.pop_back();
        return key;
    };
    auto number = [] (const std::string& name)      // "verse 3" -> 3, "verse" -> 0
    {
        const auto key = nameKey (name);
        std::size_t i = key.size();
        while (i > 0 && std::isdigit (static_cast<unsigned char> (key[i - 1])))
            --i;
        return i < key.size() && key.size() - i <= 3 ? std::stoi (key.substr (i)) : 0;
    };

    std::vector<Change> changes;
    std::vector<std::vector<Bar>> blockBars (blocks.size());
    std::size_t cursor = 0;
    for (std::size_t n = 0; n < blocks.size(); ++n)
    {
        const auto& b = blocks[n];
        if (b.label)
            continue;
        blockBars[n] = parseLines (b.lines, result.unknown);
        int target = -1;
        for (std::size_t i = cursor; i < sections.size() && target < 0; ++i)
            if (! assigned[i] && nameKey (sections[i].name) == nameKey (b.name))
                target = static_cast<int> (i);
        for (std::size_t i = 0; i < sections.size() && target < 0; ++i)
            if (! assigned[i] && nameKey (sections[i].name) == nameKey (b.name))
                target = static_cast<int> (i);
        if (target < 0 && ! base (b.name).empty())
        {
            // No part of exactly that name. "[verse 3]": the third part named
            // "verse"; "[verse]": the next of the parts "verse 1", "verse 2", ...
            const int wanted = number (b.name);
            int seen = 0;
            for (std::size_t i = 0; i < sections.size() && target < 0; ++i)
            {
                if (base (sections[i].name) != base (b.name))
                    continue;
                ++seen;
                if (! assigned[i] && (wanted > 0 ? (seen == wanted && number (sections[i].name) == 0)
                                                 : (i >= cursor && number (sections[i].name) > 0)))
                    target = static_cast<int> (i);
            }
        }
        if (target < 0)
        {
            result.unmatched.push_back ("[" + b.name + "]");
            continue;
        }
        const auto& sec = sections[static_cast<std::size_t> (target)];
        assigned[static_cast<std::size_t> (target)] = true;
        cursor = static_cast<std::size_t> (target) + 1;
        place (blockBars[n], sec.start, sec.end, true, sig, changes);
        ++result.partsFilled;
    }
    // A part still without chords: the first block written for its name
    // ("[verse]" for the second verse too, also when the parts are numbered).
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        if (assigned[i])
            continue;
        for (std::size_t n = 0; n < blocks.size(); ++n)
        {
            const auto& b = blocks[n];
            if (b.label || blockBars[n].empty())
                continue;
            if (nameKey (b.name) != nameKey (sections[i].name)
                && ! (number (b.name) == 0 && ! base (b.name).empty() && base (b.name) == base (sections[i].name)))
                continue;
            assigned[i] = true;
            place (blockBars[n], sections[i].start, sections[i].end, true, sig, changes);
            ++result.partsFilled;
            break;
        }
    }
    for (const auto& b : blocks)
    {
        if (! b.label)
            continue;
        const auto bars = parseLines (b.lines, result.unknown);
        bool any = false;
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            if (assigned[i])
                continue;
            if (std::none_of (b.names.begin(), b.names.end(), [&] (const std::string& n) { return kindsMatch (n, sections[i]); }))
                continue;
            assigned[i] = true;
            any = true;
            place (bars, sections[i].start, sections[i].end, true, sig, changes);
            ++result.partsFilled;
        }
        if (! any)
            result.unmatched.push_back (b.name + ":");
    }

    // Changes -> spans: a "set" chord starts a span, "-" ends it, an empty bar continues.
    std::stable_sort (changes.begin(), changes.end(), [] (const Change& a, const Change& b)
                      { return a.at < b.at - kEps || (std::abs (a.at - b.at) <= kEps && a.order < b.order); });
    ChordSpan open;
    bool isOpen = false;
    auto close = [&] (double at)
    {
        if (isOpen && at > open.start + kEps)
        {
            open.end = at;
            if (! result.chords.empty() && result.chords.back().chord == open.chord
                && std::abs (result.chords.back().end - open.start) < kEps)
                result.chords.back().end = at;
            else
                result.chords.push_back (open);
        }
        isOpen = false;
    };
    for (const auto& change : changes)
    {
        close (change.at);
        if (change.chord.isChord())
        {
            open = ChordSpan();
            open.start = change.at;
            open.chord = change.chord;
            open.confidence = 1.0;
            isOpen = true;
        }
    }
    return result;
}

}  // namespace boothcue
