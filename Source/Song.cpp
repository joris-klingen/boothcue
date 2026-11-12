#include "Song.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace boothcue
{

namespace
{
    constexpr double kEps = 1.0e-6;

    // A count for the display from a position the host gave. A host can hand
    // over anything (NaN, 1e300); casting that to int is undefined.
    int whole (double v)
    {
        return std::isfinite (v) ? static_cast<int> (std::clamp (v, -1.0e9, 1.0e9)) : 0;
    }

    bool validLine (const std::vector<LyricBlock>& blocks, int block, int line)
    {
        return block >= 0 && block < static_cast<int> (blocks.size())
            && line >= 0 && line < static_cast<int> (blocks[static_cast<std::size_t> (block)].lines.size());
    }
}

std::string nameKey (const std::string& s)
{
    std::string out;
    for (const char c : s)
    {
        const auto u = static_cast<unsigned char> (c);
        if (u >= 0x80)                        out += c;   // keep UTF-8 bytes as they are
        else if (u >= 'A' && u <= 'Z')        out += static_cast<char> (u - 'A' + 'a');
        else if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9')) out += c;
    }

    // Section words that mean the same thing (Dutch / English / short forms),
    // so a [Refrein] links to a Chorus clip. The trailing number is kept.
    static const std::pair<const char*, const char*> aliases[] = {
        { "refrein", "chorus" },      { "refrain", "chorus" },
        { "brug", "bridge" },         { "middle", "bridge" },
        { "couplet", "verse" },       { "vers", "verse" },        { "strofe", "verse" },
        { "pre", "prechorus" },       { "voorrefrein", "prechorus" }, { "prerefrein", "prechorus" },
        { "tussenspel", "interlude" },
    };
    std::size_t stem = 0;
    while (stem < out.size() && out[stem] >= 'a' && out[stem] <= 'z')
        ++stem;
    const auto word = out.substr (0, stem);
    for (const auto& [from, to] : aliases)
        if (word == from)
            return std::string (to) + out.substr (stem);
    return out;
}

SectionKind sectionKind (const std::string& name)
{
    // The first word decides, with anything after it ("intro v2", "verse b",
    // "chorus 2") ignored. Pre-chorus is checked before chorus.
    const auto key = nameKey (name);
    auto starts = [&key] (const char* w) { return key.rfind (w, 0) == 0; };
    if (starts ("intro"))                                     return SectionKind::intro;
    if (starts ("prechorus"))                                 return SectionKind::prechorus;
    if (starts ("verse"))                                     return SectionKind::verse;
    if (starts ("chorus") || starts ("hook"))                 return SectionKind::chorus;
    if (starts ("bridge"))                                    return SectionKind::bridge;
    if (starts ("outro") || starts ("slot") || starts ("coda") || starts ("ending"))
        return SectionKind::outro;
    return SectionKind::other;
}

std::vector<double> spreadOffsets (const LyricBlock& block, double length, double sectionStart, double barLength)
{
    const auto n = block.lines.size();
    std::vector<double> out (n, 0.0);

    std::size_t i = 0;
    while (i < n)
    {
        if (block.lines[i].timed())
        {
            out[i] = block.lines[i].offset;
            ++i;
            continue;
        }

        // A run of untimed lines [i, j).
        std::size_t j = i;
        while (j < n && ! block.lines[j].timed()) ++j;

        const bool   fromStart = (i == 0);
        const double a = fromStart ? 0.0 : block.lines[i - 1].offset;
        const double b = std::max (a, j < n ? block.lines[j].offset : length);
        const auto   count = static_cast<double> (j - i);

        for (std::size_t k = i; k < j; ++k)
        {
            const auto step = static_cast<double> (k - i);
            // From the section start the first line sits ON the start; after a
            // tapped line the run shares the gap that follows it.
            out[k] = fromStart ? a + step * (b - a) / count
                               : a + (step + 1.0) * (b - a) / (count + 1.0);
        }

        // Snap the run to the grid: the coarsest unit that keeps the lines in
        // order, apart, and inside [a, b) (strictly after a tapped line).
        if (barLength > kEps)
        {
            for (const double unit : { barLength, barLength / 2.0, barLength / 4.0 })
            {
                std::vector<double> snapped;
                bool ok = true;
                for (std::size_t k = i; k < j && ok; ++k)
                {
                    // A run from the section start keeps its first line ON the
                    // start, even when the section begins off the bar.
                    const double snap = (k == i && fromStart)
                                            ? 0.0
                                            : std::round ((sectionStart + out[k]) / unit) * unit - sectionStart;
                    const bool afterPrevious = k == i ? (fromStart ? snap >= -kEps : snap > a + kEps)
                                                      : snap > snapped.back() + kEps;
                    ok = afterPrevious && snap < b - kEps;
                    snapped.push_back (snap);
                }
                if (ok)
                {
                    for (std::size_t k = i; k < j; ++k)
                        out[k] = std::max (0.0, snapped[k - i]);
                    break;
                }
            }
        }
        i = j;
    }
    return out;
}

// ---- Song ---------------------------------------------------------------------

void Song::setSections (std::vector<Section> s)
{
    sections_ = std::move (s);
    std::stable_sort (sections_.begin(), sections_.end(),
                      [] (const Section& a, const Section& b) { return a.start < b.start; });
    rebuild();
}

void Song::setBlocks (std::vector<LyricBlock> b)
{
    blocks_ = std::move (b);
    rebuild();
}

double positionToPpq (int bar, int beat, int sixteenth, TimeSig sig)
{
    return (bar - 1) * sig.barLength() + (beat - 1) * sig.beatLength() + (sixteenth - 1) * 0.25;
}

void Song::setTimeSig (TimeSig sig)
{
    const bool changed = sig.num != sig_.num || sig.den != sig_.den;
    sig_ = sig;
    if (changed || std::abs (barLength_ - sig.barLength()) > kEps)
    {
        barLength_ = sig.barLength();
        rebuild();
    }
}

void Song::setBarLength (double beats)
{
    beats = std::max (0.0, beats);
    if (std::abs (beats - barLength_) < kEps)
        return;
    barLength_ = beats;
    rebuild();
}

double Song::end() const
{
    double e = 0.0;
    for (const auto& s : sections_)
        e = std::max (e, s.end);
    return e;
}

void Song::rebuild()
{
    std::vector<std::string> blockKeys;
    for (const auto& b : blocks_)
        blockKeys.push_back (nameKey (b.header));

    auto blocksWithKey = [&blockKeys] (const std::string& key)
    {
        std::vector<int> out;
        if (! key.empty())
            for (std::size_t i = 0; i < blockKeys.size(); ++i)
                if (blockKeys[i] == key)
                    out.push_back (static_cast<int> (i));
        return out;
    };
    auto pick = [] (const std::vector<int>& candidates, int oneBased)
    {
        const auto n = static_cast<int> (candidates.size());
        return candidates[static_cast<std::size_t> (std::clamp (oneBased, 1, n) - 1)];
    };

    sectionBlock_.assign (sections_.size(), -1);
    std::vector<std::string> seenKeys;

    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        const auto key = nameKey (sections_[i].name);
        seenKeys.push_back (key);
        const auto occurrence = static_cast<int> (std::count (seenKeys.begin(), seenKeys.end(), key));

        // 1. Same name. Several blocks with that name: the nth one belongs to
        //    the nth section of that name (the last is reused after that).
        auto same = blocksWithKey (key);
        if (! same.empty())
        {
            sectionBlock_[i] = pick (same, occurrence);
            continue;
        }

        // 2. Sections all called "Verse", blocks numbered [Verse 1], [Verse 2].
        auto numbered = blocksWithKey (key + std::to_string (occurrence));
        if (! key.empty() && ! numbered.empty())
        {
            sectionBlock_[i] = numbered.front();
            continue;
        }

        // 3. Sections numbered "Chorus 1", "Chorus 2", blocks all called [Chorus].
        auto digitsAt = key.size();
        while (digitsAt > 0 && key[digitsAt - 1] >= '0' && key[digitsAt - 1] <= '9') --digitsAt;
        if (digitsAt > 0 && digitsAt < key.size())
        {
            auto base = blocksWithKey (key.substr (0, digitsAt));
            if (! base.empty())
                sectionBlock_[i] = pick (base, std::atoi (key.c_str() + digitsAt));
        }
    }

    // Lines imported with a song position ("@13.2.3"): their offset in the
    // first section that sings their block.
    for (std::size_t b = 0; b < blocks_.size(); ++b)
    {
        int first = -1;
        for (std::size_t i = 0; i < sectionBlock_.size() && first < 0; ++i)
            if (sectionBlock_[i] == static_cast<int> (b))
                first = static_cast<int> (i);
        if (first < 0)
            continue;
        const auto& sec = sections_[static_cast<std::size_t> (first)];
        for (auto& l : blocks_[b].lines)
        {
            if (! l.hasAt)
                continue;
            const double offset = positionToPpq (l.atBar, l.atBeat, l.atSixteenth, sig_) - sec.start;
            if (offset >= -kEps && offset < sec.length() - kEps)
                l.offset = std::max (0.0, offset);
            l.hasAt = false;
        }
    }

    cues_.clear();
    for (std::size_t i = 0; i < sections_.size(); ++i)
    {
        const int b = sectionBlock_[i];
        if (b < 0)
            continue;

        const auto& block   = blocks_[static_cast<std::size_t> (b)];
        const auto  length  = sections_[i].length();
        const auto  offsets = spreadOffsets (block, length, sections_[i].start, barLength_);

        for (std::size_t l = 0; l < block.lines.size(); ++l)
        {
            if (offsets[l] >= length - kEps)
                continue;   // tapped in a longer section: falls outside this one

            Cue c;
            c.ppq     = sections_[i].start + offsets[l];
            c.section = static_cast<int> (i);
            c.block   = b;
            c.line    = static_cast<int> (l);
            c.timed   = block.lines[l].timed();
            cues_.push_back (c);
        }
    }
    std::stable_sort (cues_.begin(), cues_.end(), [] (const Cue& a, const Cue& b) { return a.ppq < b.ppq; });
}

void Song::setChords (std::vector<ChordSpan> chords, Key key)
{
    chords_ = std::move (chords);
    std::stable_sort (chords_.begin(), chords_.end(), [] (const ChordSpan& a, const ChordSpan& b) { return a.start < b.start; });
    key_ = key;
}

int Song::chordAt (double ppq) const
{
    const auto it = std::upper_bound (chords_.begin(), chords_.end(), ppq + kEps,
                                      [] (double p, const ChordSpan& c) { return p < c.start; });
    if (it == chords_.begin())
        return -1;
    const auto index = static_cast<int> (it - chords_.begin()) - 1;
    return ppq < chords_[static_cast<std::size_t> (index)].end - kEps ? index : -1;
}

int Song::previousChordBefore (double ppq) const
{
    const auto it = std::upper_bound (chords_.begin(), chords_.end(), ppq + kEps,
                                      [] (double p, const ChordSpan& c) { return p < c.start; });
    const auto after = static_cast<int> (it - chords_.begin());   // chords starting after ppq
    const int current = chordAt (ppq);
    const int before = (current >= 0 ? current : after) - 1;
    return before >= 0 ? before : -1;
}

int Song::nextChordAfter (double ppq) const
{
    const auto it = std::upper_bound (chords_.begin(), chords_.end(), ppq + kEps,
                                      [] (double p, const ChordSpan& c) { return p < c.start; });
    return it == chords_.end() ? -1 : static_cast<int> (it - chords_.begin());
}

int Song::sectionAt (double ppq) const
{
    for (std::size_t i = 0; i < sections_.size(); ++i)
        if (ppq >= sections_[i].start - kEps && ppq < sections_[i].end - kEps)
            return static_cast<int> (i);
    return -1;
}

int Song::nextSectionAfter (double ppq) const
{
    for (std::size_t i = 0; i < sections_.size(); ++i)
        if (sections_[i].start > ppq + kEps)
            return static_cast<int> (i);
    return -1;
}

int Song::blockForSection (int section) const
{
    return section >= 0 && section < static_cast<int> (sectionBlock_.size())
               ? sectionBlock_[static_cast<std::size_t> (section)] : -1;
}

int Song::firstSectionForBlock (int block) const
{
    for (std::size_t i = 0; i < sectionBlock_.size(); ++i)
        if (sectionBlock_[i] == block)
            return static_cast<int> (i);
    return -1;
}

int Song::nextCueAfter (double ppq) const
{
    const auto it = std::upper_bound (cues_.begin(), cues_.end(), ppq + kEps,
                                      [] (double p, const Cue& c) { return p < c.ppq; });
    return it == cues_.end() ? -1 : static_cast<int> (it - cues_.begin());
}

int Song::cueAt (double ppq) const
{
    const auto it = std::upper_bound (cues_.begin(), cues_.end(), ppq + kEps,
                                      [] (double p, const Cue& c) { return p < c.ppq; });
    if (it == cues_.begin())
        return -1;

    const auto index = static_cast<int> (it - cues_.begin()) - 1;
    const auto& section = sections_[static_cast<std::size_t> (cues_[static_cast<std::size_t> (index)].section)];
    return ppq < section.end - kEps ? index : -1;
}

double Song::resolvedOffset (int block, int line) const
{
    if (! validLine (blocks_, block, line))
        return -1.0;
    const int section = firstSectionForBlock (block);
    if (section < 0)
        return -1.0;
    const auto& sec = sections_[static_cast<std::size_t> (section)];
    const auto offsets = spreadOffsets (blocks_[static_cast<std::size_t> (block)], sec.length(), sec.start, barLength_);
    return offsets[static_cast<std::size_t> (line)];
}

bool Song::stamp (int block, int line, double ppq)
{
    if (! validLine (blocks_, block, line))
        return false;
    const int section = sectionAt (ppq);
    if (section < 0 || blockForSection (section) != block)
        return false;

    auto& lines = blocks_[static_cast<std::size_t> (block)].lines;
    const double offset = std::max (0.0, ppq - sections_[static_cast<std::size_t> (section)].start);

    for (std::size_t l = 0; l < lines.size(); ++l)
    {
        if (! lines[l].timed() || static_cast<int> (l) == line)
            continue;
        const bool before = static_cast<int> (l) < line;
        if ((before && lines[l].offset >= offset - kEps) || (! before && lines[l].offset <= offset + kEps))
            lines[l].offset = -1.0;
    }
    lines[static_cast<std::size_t> (line)].offset = offset;
    rebuild();
    return true;
}

bool Song::nudge (int block, int line, double delta)
{
    const double base = resolvedOffset (block, line);
    if (base < 0.0)
        return false;

    auto& lines = blocks_[static_cast<std::size_t> (block)].lines;
    double lo = 0.0, hi = 1.0e12;
    for (int l = line - 1; l >= 0; --l)
        if (lines[static_cast<std::size_t> (l)].timed()) { lo = lines[static_cast<std::size_t> (l)].offset; break; }
    for (std::size_t l = static_cast<std::size_t> (line) + 1; l < lines.size(); ++l)
        if (lines[l].timed()) { hi = lines[l].offset; break; }

    lines[static_cast<std::size_t> (line)].offset = std::clamp (base + delta, lo, std::max (lo, hi));
    rebuild();
    return true;
}

void Song::clearTiming (int block, int line)
{
    if (! validLine (blocks_, block, line))
        return;
    blocks_[static_cast<std::size_t> (block)].lines[static_cast<std::size_t> (line)].offset = -1.0;
    rebuild();
}

void Song::clearAllTimings()
{
    for (auto& b : blocks_)
        for (auto& l : b.lines)
            l.offset = -1.0;
    rebuild();
}

// ---- tap-along -------------------------------------------------------------------

TapOutcome tapAt (Song& song, TapCursor& cursor, double ppq)
{
    TapOutcome out;
    const int section = song.sectionAt (ppq);
    if (section < 0)
    {
        out.error = "The playhead is not in a section";
        return out;
    }
    const int block = song.blockForSection (section);
    if (block < 0)
    {
        out.error = "No lyrics for " + song.sections()[static_cast<std::size_t> (section)].name;
        return out;
    }
    const auto lines = static_cast<int> (song.blocks()[static_cast<std::size_t> (block)].lines.size());
    if (lines == 0)
    {
        out.error = "[" + song.blocks()[static_cast<std::size_t> (block)].header + "] has no lines";
        return out;
    }

    if (cursor.block != block || cursor.line < 0 || (cursor.line >= lines && section != cursor.section))
    {
        cursor.block = block;
        cursor.line = 0;
    }
    if (cursor.line >= lines)
    {
        out.error = "Every line of [" + song.blocks()[static_cast<std::size_t> (block)].header
                    + "] is tapped: select a line to tap it again";
        return out;
    }

    if (! song.stamp (block, cursor.line, ppq))
    {
        out.error = "Could not place the line here";
        return out;
    }
    out.ok = true;
    out.block = block;
    out.line = cursor.line;
    out.section = section;
    cursor.section = section;
    ++cursor.line;
    return out;
}

// ---- bars, beats, count-in ------------------------------------------------------

BarBeat barBeatAt (double ppq, TimeSig sig, bool hasLastBarStart, double lastBarStart)
{
    const double barLen  = sig.barLength();
    const double beatLen = sig.beatLength();

    const double barIndex = std::floor ((ppq + kEps) / barLen);
    const double barStart = hasLastBarStart ? lastBarStart : barIndex * barLen;

    BarBeat out;
    out.bar  = whole (hasLastBarStart ? std::floor ((lastBarStart + kEps) / barLen + 0.5) : barIndex) + 1;
    out.beat = std::clamp (whole (std::floor ((ppq - barStart + kEps) / beatLen)), 0, std::max (0, sig.num - 1)) + 1;
    return out;
}

std::string formatPosition (double ppq, TimeSig sig)
{
    const auto bb = barBeatAt (ppq, sig);
    const double barStart  = std::floor ((ppq + kEps) / sig.barLength()) * sig.barLength();
    const double beatStart = barStart + (bb.beat - 1) * sig.beatLength();
    // Real 16th notes (a quarter of a quarter note): 4 per beat in x/4, 2 in x/8.
    const int perBeat = std::max (1, static_cast<int> (std::round (sig.beatLength() / 0.25)));
    const int sixteenth = std::clamp (1 + whole (std::floor ((ppq - beatStart) / 0.25 + kEps)), 1, perBeat);
    return std::to_string (bb.bar) + "." + std::to_string (bb.beat) + "." + std::to_string (sixteenth);
}

Countdown countdownAt (const Song& song, double ppq, TimeSig sig, const CountSettings& settings)
{
    Countdown none;
    if (! settings.enabled)
        return none;

    const int next = song.nextCueAfter (ppq);
    if (next < 0)
        return none;

    const auto& cues = song.cues();
    const auto& cue  = cues[static_cast<std::size_t> (next)];
    const double barLen = sig.barLength();

    // Only vocal ENTRIES are counted in: the first line, or a line after a rest.
    if (next > 0)
    {
        const double gap = cue.ppq - cues[static_cast<std::size_t> (next) - 1].ppq;
        if (gap < settings.entryGapBars * barLen - kEps)
            return none;
    }

    const double remaining = cue.ppq - ppq;
    const double bars = remaining / barLen;

    Countdown out;
    out.cue = next;
    if (bars <= 1.0 + kEps)
    {
        out.kind  = Countdown::Kind::beats;
        out.value = std::max (1, whole (std::ceil (remaining / sig.beatLength() - kEps)));
        return out;
    }

    const int barsLeft = whole (std::ceil (bars - kEps)) - 1;   // full bars before the counted bar
    if (barsLeft > settings.countInBars)
        return none;

    out.kind  = Countdown::Kind::bars;
    out.value = barsLeft;
    return out;
}

}  // namespace boothcue
