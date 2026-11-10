#include "LyricImport.h"

#include <algorithm>
#include <map>

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

std::vector<LyricBlock> parseLyrics (const std::string& input)
{
    std::vector<LyricBlock> blocks;

    std::size_t pos = 0;
    if (input.compare (0, 3, "\xEF\xBB\xBF") == 0)
        pos = 3;   // UTF-8 byte order mark

    while (pos <= input.size())
    {
        auto eol = input.find ('\n', pos);
        if (eol == std::string::npos)
            eol = input.size();
        const auto line = trim (input.substr (pos, eol - pos));
        pos = eol + 1;

        if (line.empty())
            continue;

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            LyricBlock b;
            b.header = trim (line.substr (1, line.size() - 2));
            blocks.push_back (b);
            continue;
        }

        if (blocks.empty())
            blocks.emplace_back();
        LyricLine l;
        l.text = line;
        blocks.back().lines.push_back (l);
    }
    return blocks;
}

std::string lyricsToText (const std::vector<LyricBlock>& blocks)
{
    std::string out;
    for (const auto& b : blocks)
    {
        if (! out.empty())
            out += '\n';
        if (! b.header.empty())
            out += '[' + b.header + "]\n";
        for (const auto& l : b.lines)
            out += l.text + '\n';
    }
    return out;
}

void keepTimings (const std::vector<LyricBlock>& previous, std::vector<LyricBlock>& edited)
{
    const auto oldIds = occurrences (previous);
    const auto newIds = occurrences (edited);

    for (std::size_t n = 0; n < edited.size(); ++n)
        for (std::size_t o = 0; o < previous.size(); ++o)
        {
            if (oldIds[o] != newIds[n])
                continue;

            // Pair the lines up by the longest common subsequence of their
            // texts, so inserting, deleting or changing one line keeps the
            // timing of every other line (and the order of the taps).
            auto& lines = edited[n].lines;
            const auto& old = previous[o].lines;
            const auto a = old.size(), b = lines.size();
            std::vector<std::vector<int>> lcs (a + 1, std::vector<int> (b + 1, 0));
            for (std::size_t i = a; i-- > 0;)
                for (std::size_t j = b; j-- > 0;)
                    lcs[i][j] = old[i].text == lines[j].text ? lcs[i + 1][j + 1] + 1
                                                             : std::max (lcs[i + 1][j], lcs[i][j + 1]);
            std::size_t i = 0, j = 0;
            while (i < a && j < b)
            {
                if (old[i].text == lines[j].text)       { lines[j].offset = old[i].offset; ++i; ++j; }
                else if (lcs[i + 1][j] >= lcs[i][j + 1]) ++i;
                else                                     ++j;
            }
            break;
        }
}

}  // namespace boothcue
