#include "LyricImport.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
#include <sstream>

namespace boothcue
{

namespace
{
    std::string trim (const std::string& s)
    {
        const char* ws = " \t\r\n";
        const auto a = s.find_first_not_of (ws);
        if (a == std::string::npos)
            return {};
        return s.substr (a, s.find_last_not_of (ws) - a + 1);
    }

    // Per block: its header key and its position among the blocks sharing it.
    std::vector<std::pair<std::string, int>> occurrences (const std::vector<LyricBlock>& blocks)
    {
        std::vector<std::pair<std::string, int>> out;
        std::map<std::string, int> seen;
        for (const auto& b : blocks)
        {
            const auto key = nameKey (b.header);
            out.emplace_back (key, seen[key]++);
        }
        return out;
    }
}

namespace
{
    // Google Docs pastes non-breaking spaces; treat them as spaces.
    std::string normaliseSpaces (std::string line)
    {
        for (std::size_t i = 0; (i = line.find ("\xC2\xA0", i)) != std::string::npos;)
            line.replace (i, 2, " ");
        for (auto& c : line)
            if (c == '\t' || c == '\r')
                c = ' ';
        return line;
    }

    std::string collapseSpaces (const std::string& s)
    {
        std::string out;
        for (const char c : s)
            if (c != ' ' || (! out.empty() && out.back() != ' '))
                out += c;
        return trim (out);
    }

    std::string lower (std::string s)
    {
        for (auto& c : s)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return s;
    }

    // ---- chord charts ------------------------------------------------------------

    bool isChordToken (std::string t)
    {
        if (t.size() >= 2 && t.front() == '(' && t.back() == ')')
            t = t.substr (1, t.size() - 2);   // an alternative: "(Bb)", "(Csus4)"
        static const std::regex chord (R"(^[A-G][#b]?[a-zA-Z0-9#b+()\-]*(/[A-G][#b]?)?$)");
        return std::regex_match (t, chord);
    }

    // "Verse: ||: Am | Dm E(7b13) :||", "   |  Ab  | Cm  :||", "(Roeren:) | Dm | C ||":
    // a bar line, and nothing after it but chords, repeat signs and "-" / "%".
    bool isChordChart (const std::string& line)
    {
        const auto bar = line.find ('|');
        if (bar == std::string::npos)
            return false;
        std::string rest = line.substr (bar);
        for (auto& c : rest)
            if (c == '|' || c == ':')
                c = ' ';
        std::istringstream words (rest);
        std::string w;
        while (words >> w)
        {
            const auto l = lower (w);
            if (w == "-" || w == "%" || l == "x2" || l == "2x" || l == "(2x)" || l == "(x2)" || l == "x3" || l == "3x")
                continue;
            if (! isChordToken (w))
                return false;
        }
        return true;
    }

    // ---- headers -------------------------------------------------------------------

    struct Header
    {
        std::string name;      // as shown: "Verse 1", "Refrein", "Brug"
        std::string inline_;   // text after "Header:" on the same line, if any
    };

    std::string capitalised (std::string s)
    {
        if (! s.empty() && s[0] >= 'a' && s[0] <= 'z')
            s[0] = static_cast<char> (s[0] - 'a' + 'A');
        return s;
    }

    // "[Chorus]", "[Chorus x2]", "verse 1", "Refrein:", "Refrein x2",
    // "Brug: Ik heb bijna niks…", "*pre*", "## Bridge".
    bool parseHeader (const std::string& line, Header& out)
    {
        static const std::regex repeat (R"(\s*\(?\s*(x\s*\d+|\d+\s*x)\s*\)?\s*$)", std::regex::icase);

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            out.name = capitalised (trim (std::regex_replace (line.substr (1, line.size() - 2), repeat, "")));
            out.inline_.clear();
            return true;
        }

        // Markdown / rich-text leftovers around a header word.
        std::string t = line;
        while (! t.empty() && (t.front() == '*' || t.front() == '_' || t.front() == '#' || t.front() == ' '))
            t.erase (0, 1);
        while (! t.empty() && (t.back() == '*' || t.back() == '_' || t.back() == ' '))
            t.pop_back();

        static const std::regex word (
            R"(^(intro|verse|vers|couplet|strofe|pre[ -]?chorus|prechorus|pre|voorrefrein|chorus|refrein|refrain|)"
            R"(bridge|brug|middle ?8|outro|slot|solo|interlude|tussenspel|instrumental|hook|tag|coda|break))"
            R"((\s*\d+)?\s*(\(?\s*(?:x\s*\d+|\d+\s*x)\s*\)?)?\s*(:\s*(.*))?$)",
            std::regex::icase);
        std::smatch m;
        if (! std::regex_match (t, m, word))
            return false;
        out.name = capitalised (collapseSpaces (m[1].str() + m[2].str()));
        out.inline_ = m[5].matched ? collapseSpaces (m[5].str()) : std::string();
        while (! out.inline_.empty() && (out.inline_.back() == '*' || out.inline_.back() == '_'))
            out.inline_.pop_back();
        return true;
    }

    // ---- lines -----------------------------------------------------------------------

    // "meneer! x 2" -> ("meneer!", 2). Only a marker after a space counts.
    int repeatCount (std::string& text)
    {
        static const std::regex marker (R"(^(.*\S)\s+\(?\s*(?:x\s*(\d)|(\d)\s*x)\s*\)?$)", std::regex::icase);
        std::smatch m;
        if (! std::regex_match (text, m, marker))
            return 1;
        const int n = std::stoi (m[2].matched ? m[2].str() : m[3].str());
        if (n < 2 || n > 8)
            return 1;
        text = m[1].str();
        return n;
    }

    bool isPickup (const std::string& line)
    {
        if (line.size() < 3 || line.front() != '(' || line.back() != ')')
            return false;
        if (line.find (')') != line.size() - 1)
            return false;   // "(a) b (c)": more than one group
        return std::count (line.begin(), line.end(), ' ') <= 2;   // at most 3 words
    }

    // Lowercase letters and digits only, line by line: the same stanza written
    // again matches whatever its punctuation and capitals.
    std::string stanzaKey (const std::vector<std::string>& lines)
    {
        std::string key;
        for (const auto& l : lines)
        {
            for (const char c : l)
            {
                const auto u = static_cast<unsigned char> (c);
                if (u >= 0x80 || std::isalnum (u))
                    key += static_cast<char> (std::tolower (u));
            }
            key += '\n';
        }
        return key;
    }

    struct Stanza
    {
        std::vector<std::string> lines;
    };
}

std::vector<LyricBlock> parseLyrics (const std::string& input, LyricImportReport* reportOut)
{
    LyricImportReport report;
    std::vector<LyricBlock> blocks;

    // ---- pass 1: lines into headers, chord charts, stanzas -----------------------------
    struct Item
    {
        enum class Kind { header, stanza, gap } kind;
        Header header;
        Stanza stanza;
    };
    std::vector<Item> items;

    std::size_t pos = 0;
    if (input.compare (0, 3, "\xEF\xBB\xBF") == 0)
        pos = 3;   // UTF-8 byte order mark

    auto endStanza = [&items]
    {
        if (! items.empty() && items.back().kind == Item::Kind::stanza)
            items.push_back ({ Item::Kind::gap, {}, {} });
    };
    auto addLine = [&items] (const std::string& text)
    {
        if (items.empty() || items.back().kind != Item::Kind::stanza)
            items.push_back ({ Item::Kind::stanza, {}, {} });
        items.back().stanza.lines.push_back (text);
    };

    while (pos <= input.size())
    {
        auto eol = input.find ('\n', pos);
        if (eol == std::string::npos)
            eol = input.size();
        auto line = collapseSpaces (normaliseSpaces (input.substr (pos, eol - pos)));
        pos = eol + 1;

        // No lyric line is this long. A file that isn't lyrics (one endless
        // line) is cut here, between two characters: the patterns below are
        // std::regex, which recurses per character and runs out of stack on
        // a line of a few hundred thousand.
        constexpr std::size_t kMaxLine = 1000;
        if (line.size() > kMaxLine)
        {
            auto cut = kMaxLine;
            while (cut > 0 && (static_cast<unsigned char> (line[cut]) & 0xC0) == 0x80)
                --cut;
            line = collapseSpaces (line.substr (0, cut));
        }

        // Empty "()" placeholders in a line are notes to self, not lyrics.
        for (std::size_t i; (i = line.find ("()")) != std::string::npos;)
            line.erase (i, 2);
        line = collapseSpaces (line);

        if (line.empty())
        {
            endStanza();
            if (! items.empty() && items.back().kind == Item::Kind::header)
                items.push_back ({ Item::Kind::gap, {}, {} });
            continue;
        }
        // "# ..." is a comment (BoothCue's export starts with one), unless it
        // is a section header written as a markdown heading ("## Bridge").
        if (line[0] == '#')
        {
            Header h;
            if (! parseHeader (line, h))
                continue;
        }
        if (isChordChart (line))
        {
            report.chordCharts.push_back (line);
            endStanza();
            continue;
        }
        Header h;
        if (parseHeader (line, h))
        {
            items.push_back ({ Item::Kind::header, h, {} });
            if (! h.inline_.empty())
                addLine (h.inline_);
            continue;
        }
        addLine (line);
    }

    // ---- a title: a short single line opening the text, set apart by a blank line
    //      or a chord chart (not run straight into a header), before more follows -----
    {
        std::size_t first = 0;
        while (first < items.size() && items[first].kind == Item::Kind::gap)
            ++first;
        if (first + 1 < items.size() && items[first].kind == Item::Kind::stanza
            && items[first].stanza.lines.size() == 1 && items[first + 1].kind == Item::Kind::gap)
        {
            const auto& t = items[first].stanza.lines[0];
            const bool shortLine = std::count (t.begin(), t.end(), ' ') <= 6;
            const bool punctuated = ! t.empty() && std::string (",.;:!?").find (t.back()) != std::string::npos;
            const bool ellipsis = t.size() >= 3 && t.compare (t.size() - 3, 3, "\xE2\x80\xA6") == 0;
            bool moreFollows = false;
            for (std::size_t i = first + 1; i < items.size(); ++i)
                moreFollows = moreFollows || items[i].kind != Item::Kind::gap;
            if (shortLine && ! punctuated && ! ellipsis && moreFollows)
            {
                report.title = t;
                items.erase (items.begin() + static_cast<std::ptrdiff_t> (first));
            }
        }
    }

    // ---- pass 2: stanzas into blocks --------------------------------------------------
    // stanza text -> the header it was first sung under, and whether it opened that part
    std::map<std::string, std::pair<std::string, bool>> stanzaHeader;
    bool stanzaOpensBlock = false;
    std::vector<std::string> usedKeys;
    auto used = [&usedKeys] (const std::string& key)
    {
        return std::find (usedKeys.begin(), usedKeys.end(), key) != usedKeys.end();
    };

    bool havePending = false, gapAfterPending = false;
    Header pending;
    bool haveCurrent = false;

    auto openBlock = [&] (const std::string& header)
    {
        LyricBlock b;
        b.header = header;
        blocks.push_back (b);
        haveCurrent = true;
        usedKeys.push_back (nameKey (header));
    };
    // A header that got no lines of its own.
    auto settlePending = [&]
    {
        if (! havePending)
            return;
        havePending = false;
        if (used (nameKey (pending.name)))
        {
            ++report.repeatMarkers;   // "Refrein": this part again
            haveCurrent = false;      // what follows isn't part of it
        }
        else
        {
            openBlock (pending.name);   // e.g. an instrumental [Solo]
        }
    };

    for (std::size_t i = 0; i < items.size(); ++i)
    {
        auto& item = items[i];
        if (item.kind == Item::Kind::gap)
        {
            if (havePending)
                gapAfterPending = true;
            continue;
        }
        if (item.kind == Item::Kind::header)
        {
            settlePending();
            report.anyHeader = true;
            pending = item.header;
            havePending = true;
            gapAfterPending = false;
            continue;
        }

        // A stanza.
        auto lines = item.stanza.lines;
        const auto key = stanzaKey (lines);

        const auto blocksBefore = blocks.size();
        const auto match = stanzaHeader.find (key);
        const bool repeatsAnOpening = match != stanzaHeader.end() && ! match->second.first.empty();

        if (havePending)
        {
            const bool marker = gapAfterPending && used (nameKey (pending.name));
            if (marker)
            {
                settlePending();
                openBlock ({});   // header-less: the editor shows it as not linked
            }
            else
            {
                havePending = false;
                openBlock (pending.name);
            }
        }
        else if (repeatsAnOpening && (match->second.second || (haveCurrent && nameKey (blocks.back().header) == nameKey (match->second.first))))
        {
            // The same stanza again, without a header: a repeat of the part it
            // opened (or the next stanza of a part being repeated right now).
            ++report.stanzasMatched;
            if (! haveCurrent || nameKey (blocks.back().header) != nameKey (match->second.first))
                openBlock (match->second.first);
        }
        else if (! haveCurrent)
        {
            openBlock ({});
        }

        stanzaOpensBlock = blocks.size() != blocksBefore;
        stanzaHeader.emplace (key, std::make_pair (blocks.back().header, stanzaOpensBlock));

        // Lines: join pickups, expand "x2".
        for (std::size_t l = 0; l < lines.size(); ++l)
        {
            auto text = lines[l];
            if (isPickup (text) && l + 1 < lines.size() && lines[l + 1].front() != '(')
            {
                text += " " + lines[++l];
                ++report.pickupsJoined;
            }
            // "@13.2.3 text": a start as a song position.
            LyricLine line;
            {
                // At most 6 / 3 digits: a longer number is no position (and
                // would not fit an int), the line stays text.
                static const std::regex at (R"(^@(\d{1,6})(?:\.(\d{1,3}))?(?:\.(\d{1,3}))?\s+(.*)$)");
                std::smatch m;
                if (std::regex_match (text, m, at))
                {
                    line.hasAt = true;
                    line.atBar = std::stoi (m[1].str());
                    line.atBeat = m[2].matched ? std::stoi (m[2].str()) : 1;
                    line.atSixteenth = m[3].matched ? std::stoi (m[3].str()) : 1;
                    text = m[4].str();
                    ++report.timedLines;
                }
            }
            const int times = repeatCount (text);
            report.linesRepeated += times - 1;
            line.text = text;
            for (int n = 0; n < times; ++n)
                blocks.back().lines.push_back (line);
        }
    }
    settlePending();

    if (reportOut != nullptr)
        *reportOut = report;
    return blocks;
}

std::string lyricsToTimedText (const Song& song)
{
    const auto& blocks = song.blocks();
    std::string out = "# BoothCue lyrics - \"@bar.beat.16th\" = where a tapped line starts\n\n";
    for (std::size_t b = 0; b < blocks.size(); ++b)
    {
        // As lyricsToText: no blank line after opening lines without a header.
        if (b > 0 && ! blocks[b].header.empty() && ! (b == 1 && blocks[0].header.empty()))
            out += '\n';
        if (! blocks[b].header.empty())
            out += '[' + blocks[b].header + "]\n";
        const int section = song.firstSectionForBlock (static_cast<int> (b));
        for (std::size_t l = 0; l < blocks[b].lines.size(); ++l)
        {
            const auto& line = blocks[b].lines[l];
            if (line.timed() && section >= 0)
                out += "@" + formatPosition (song.sections()[static_cast<std::size_t> (section)].start + line.offset,
                                             song.timeSig()) + " ";
            out += line.text + '\n';
        }
    }
    return out;
}

std::string lyricsToText (const std::vector<LyricBlock>& blocks)
{
    std::string out;
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& b = blocks[i];
        // A blank line before each header, except after an opening block
        // without a header: a single line set apart there would read as a title.
        const bool afterOpeningLines = i == 1 && blocks[0].header.empty();
        if (! out.empty() && ! b.header.empty() && ! afterOpeningLines)
            out += '\n';
        if (! b.header.empty())
            out += '[' + b.header + "]\n";
        for (const auto& l : b.lines)
            out += l.text + '\n';
    }
    return out;
}

namespace
{
// Pairs the blocks and their lines up (see keepTimings) and calls
// take (old line, new line) for every pair.
template <typename Take>
void pairLines (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited, Take take);
}

void keepTimings (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited)
{
    pairLines (previous, edited, [] (const LyricLine& old, LyricLine& line) { line.offset = old.offset; });
}

void keepStarts (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited)
{
    pairLines (previous, edited, [] (const LyricLine& old, LyricLine& line)
    {
        if (! old.timed() && ! old.hasAt)
            return;
        const auto text = line.text;
        line = old;
        line.text = text;
    });
}

namespace
{
// A line as it is sung, for telling whether two texts are the same line: only
// its letters and digits, in lower case ("Lachend op de foto," = "lachend op
// de foto").
std::string sungKey (const std::string& text)
{
    std::string out;
    for (const char ch : text)
    {
        const auto c = static_cast<unsigned char> (ch);
        if (c >= 0x80 || std::isalnum (c))
            out += static_cast<char> (c < 0x80 ? std::tolower (c) : c);
    }
    return out;
}

// The header without its number: "Verse 1" and "Verse" are both "verse".
std::string baseKey (const std::string& header)
{
    auto key = nameKey (header);
    while (! key.empty() && std::isdigit (static_cast<unsigned char> (key.back())))
        key.pop_back();
    return key;
}

template <typename Take>
void pairBlock (const LyricBlock& previous, LyricBlock& edited, Take take)
{
    // Pair the lines up by the longest common subsequence of their texts (as
    // sung: a comma or a capital more or less is the same line), so
    // inserting, deleting or changing one line keeps the timing of every
    // other line (and the order of the taps).
    auto& lines = edited.lines;
    const auto& old = previous.lines;
    const auto a = old.size(), b = lines.size();
    std::vector<std::string> oldKeys, newKeys;
    for (const auto& l : old)   oldKeys.push_back (sungKey (l.text));
    for (const auto& l : lines) newKeys.push_back (sungKey (l.text));
    std::vector<std::vector<int>> lcs (a + 1, std::vector<int> (b + 1, 0));
    for (std::size_t i = a; i-- > 0;)
        for (std::size_t j = b; j-- > 0;)
            lcs[i][j] = oldKeys[i] == newKeys[j] ? lcs[i + 1][j + 1] + 1
                                                 : std::max (lcs[i + 1][j], lcs[i][j + 1]);
    // Between two lines that pair up: as many old lines as new ones means
    // those were reworded, not added or removed, and keep their place.
    std::size_t i = 0, j = 0, gapI = 0, gapJ = 0;
    auto closeGap = [&] (std::size_t endI, std::size_t endJ)
    {
        if (endI - gapI == endJ - gapJ)
            for (std::size_t k = 0; k < endI - gapI; ++k)
                take (old[gapI + k], lines[gapJ + k]);
    };
    while (i < a && j < b)
    {
        if (oldKeys[i] == newKeys[j])
        {
            closeGap (i, j);
            take (old[i], lines[j]);
            ++i;
            ++j;
            gapI = i;
            gapJ = j;
        }
        else if (lcs[i + 1][j] >= lcs[i][j + 1]) ++i;
        else                                     ++j;
    }
    closeGap (a, b);
}

template <typename Take>
void pairLines (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited, Take take)
{
    const auto oldIds = occurrences (previous);
    const auto newIds = occurrences (edited);
    std::vector<bool> oldUsed (previous.size(), false), newUsed (edited.size(), false);

    for (std::size_t n = 0; n < edited.size(); ++n)
        for (std::size_t o = 0; o < previous.size(); ++o)
            if (! oldUsed[o] && oldIds[o] == newIds[n])
            {
                oldUsed[o] = newUsed[n] = true;
                pairBlock (previous[o], edited[n], take);
                break;
            }

    // A header that got or lost its number ("Verse 1" -> "Verse"): the blocks
    // of that kind left over pair up in order.
    for (std::size_t n = 0; n < edited.size(); ++n)
    {
        if (newUsed[n] || baseKey (edited[n].header).empty())
            continue;
        for (std::size_t o = 0; o < previous.size(); ++o)
            if (! oldUsed[o] && baseKey (previous[o].header) == baseKey (edited[n].header))
            {
                oldUsed[o] = newUsed[n] = true;
                pairBlock (previous[o], edited[n], take);
                break;
            }
    }
}

}  // namespace

}  // namespace boothcue
