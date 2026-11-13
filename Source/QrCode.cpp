#include "QrCode.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace boothcue
{

namespace
{
    constexpr int kMaxVersion = 10;

    // Error correction level M, versions 1-10 (ISO/IEC 18004, table 9):
    // error correction codewords per block, and the number of blocks.
    constexpr std::array<int, kMaxVersion + 1> kEccPerBlock { 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
    constexpr std::array<int, kMaxVersion + 1> kBlocks      { 0,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5 };

    constexpr int kFormatEccM = 0;   // the two level bits in the format information: M = 00

    int sizeOf (int version) { return 17 + 4 * version; }

    // Codewords in the symbol: every module that isn't a function pattern or
    // format / version information, divided into bytes (remainder bits dropped).
    int totalCodewords (int version)
    {
        int modules = (16 * version + 128) * version + 64;
        if (version >= 2)
        {
            const int align = version / 7 + 2;
            modules -= (25 * align - 10) * align - 55;
            if (version >= 7)
                modules -= 36;
        }
        return modules / 8;
    }

    int dataCodewords (int version)
    {
        return totalCodewords (version) - kEccPerBlock[static_cast<size_t> (version)] * kBlocks[static_cast<size_t> (version)];
    }

    // Byte mode: 4 bits of mode, 8 bits of length (versions 1-9; 16 from 10).
    int headerBits (int version) { return 4 + (version <= 9 ? 8 : 16); }

    std::vector<int> alignmentCentres (int version)
    {
        if (version == 1)
            return {};
        const int count = version / 7 + 2;
        const int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
        std::vector<int> out (static_cast<size_t> (count));
        out[0] = 6;
        for (int i = count - 1, pos = sizeOf (version) - 7; i >= 1; --i, pos -= step)
            out[static_cast<size_t> (i)] = pos;
        return out;
    }

    // ---- Reed-Solomon over GF(256), polynomial x^8 + x^4 + x^3 + x^2 + 1 ----------

    std::uint8_t gfMultiply (std::uint8_t a, std::uint8_t b)
    {
        int result = 0;
        for (int i = 7; i >= 0; --i)
        {
            result = (result << 1) ^ ((result >> 7) * 0x11d);
            if ((b >> i) & 1)
                result ^= a;
        }
        return static_cast<std::uint8_t> (result);
    }

    // The generator polynomial (x - a^0)(x - a^1)...(x - a^(degree-1)), without
    // its leading 1, highest power first.
    std::vector<std::uint8_t> generator (int degree)
    {
        std::vector<std::uint8_t> g (static_cast<size_t> (degree));
        g.back() = 1;
        std::uint8_t root = 1;
        for (int i = 0; i < degree; ++i)
        {
            for (size_t j = 0; j < g.size(); ++j)
            {
                g[j] = gfMultiply (g[j], root);
                if (j + 1 < g.size())
                    g[j] ^= g[j + 1];
            }
            root = gfMultiply (root, 0x02);
        }
        return g;
    }

    std::vector<std::uint8_t> remainder (const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& gen)
    {
        std::vector<std::uint8_t> r (gen.size());
        for (const auto byte : data)
        {
            const auto factor = static_cast<std::uint8_t> (byte ^ r[0]);
            r.erase (r.begin());
            r.push_back (0);
            for (size_t i = 0; i < r.size(); ++i)
                r[i] ^= gfMultiply (gen[i], factor);
        }
        return r;
    }

    // ---- the symbol --------------------------------------------------------------

    class Symbol
    {
    public:
        explicit Symbol (int v) : version (v), size (sizeOf (v)),
                                  dark (static_cast<size_t> (size * size)),
                                  function (static_cast<size_t> (size * size)) {}

        const int version, size;
        std::vector<bool> dark, function;

        bool get (int x, int y) const { return dark[index (x, y)]; }
        void set (int x, int y, bool on) { dark[index (x, y)] = on; }
        void setFunction (int x, int y, bool on) { set (x, y, on); function[index (x, y)] = true; }
        bool isFunction (int x, int y) const { return function[index (x, y)]; }

        void drawFunctionPatterns()
        {
            for (int i = 0; i < size; ++i)   // timing patterns
            {
                setFunction (6, i, i % 2 == 0);
                setFunction (i, 6, i % 2 == 0);
            }
            drawFinder (3, 3);
            drawFinder (size - 4, 3);
            drawFinder (3, size - 4);

            const auto centres = alignmentCentres (version);
            const auto last = centres.size() - 1;
            for (size_t i = 0; i < centres.size(); ++i)
                for (size_t j = 0; j < centres.size(); ++j)
                    if (! ((i == 0 && j == 0) || (i == 0 && j == last) || (i == last && j == 0)))   // the finder corners
                        drawAlignment (centres[i], centres[j]);

            drawFormat (0);   // reserves the areas; drawn again with the chosen mask
            drawVersion();
        }

        // The format information: level M and the mask, BCH(15,5), twice.
        void drawFormat (int mask)
        {
            const int data = kFormatEccM << 3 | mask;
            int rem = data;
            for (int i = 0; i < 10; ++i)
                rem = (rem << 1) ^ ((rem >> 9) * 0x537);
            const int bits = (data << 10 | rem) ^ 0x5412;
            auto bit = [bits] (int i) { return ((bits >> i) & 1) != 0; };

            for (int i = 0; i <= 5; ++i)
                setFunction (8, i, bit (i));
            setFunction (8, 7, bit (6));
            setFunction (8, 8, bit (7));
            setFunction (7, 8, bit (8));
            for (int i = 9; i < 15; ++i)
                setFunction (14 - i, 8, bit (i));

            for (int i = 0; i < 8; ++i)
                setFunction (size - 1 - i, 8, bit (i));
            for (int i = 8; i < 15; ++i)
                setFunction (8, size - 15 + i, bit (i));
            setFunction (8, size - 8, true);   // the dark module
        }

        // Versions 7 and up: the version number, BCH(18,6), twice.
        void drawVersion()
        {
            if (version < 7)
                return;
            int rem = version;
            for (int i = 0; i < 12; ++i)
                rem = (rem << 1) ^ ((rem >> 11) * 0x1f25);
            const long bits = static_cast<long> (version) << 12 | rem;
            for (int i = 0; i < 18; ++i)
            {
                const bool on = ((bits >> i) & 1) != 0;
                const int a = size - 11 + i % 3, b = i / 3;
                setFunction (a, b, on);
                setFunction (b, a, on);
            }
        }

        // The codewords in the zig-zag: two columns at a time from the right,
        // up then down, skipping the vertical timing pattern.
        void drawCodewords (const std::vector<std::uint8_t>& codewords)
        {
            size_t i = 0;
            const size_t bits = codewords.size() * 8;
            for (int right = size - 1; right >= 1; right -= 2)
            {
                if (right == 6)
                    right = 5;
                const bool upward = ((right + 1) & 2) == 0;
                for (int step = 0; step < size; ++step)
                    for (int j = 0; j < 2; ++j)
                    {
                        const int x = right - j;
                        const int y = upward ? size - 1 - step : step;
                        if (isFunction (x, y) || i >= bits)
                            continue;   // remainder bits stay light
                        set (x, y, ((codewords[i >> 3] >> (7 - static_cast<int> (i & 7))) & 1) != 0);
                        ++i;
                    }
            }
        }

        void applyMask (int mask)
        {
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                {
                    if (isFunction (x, y))
                        continue;
                    bool flip = false;
                    switch (mask)
                    {
                        case 0:  flip = (x + y) % 2 == 0; break;
                        case 1:  flip = y % 2 == 0; break;
                        case 2:  flip = x % 3 == 0; break;
                        case 3:  flip = (x + y) % 3 == 0; break;
                        case 4:  flip = (x / 3 + y / 2) % 2 == 0; break;
                        case 5:  flip = x * y % 2 + x * y % 3 == 0; break;
                        case 6:  flip = (x * y % 2 + x * y % 3) % 2 == 0; break;
                        default: flip = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                    }
                    if (flip)
                        set (x, y, ! get (x, y));
                }
        }

        // The penalty score of ISO/IEC 18004 section 7.8.3: runs, 2x2 blocks,
        // finder-like patterns and the balance of dark and light.
        long penalty() const
        {
            long score = 0;
            for (int pass = 0; pass < 2; ++pass)   // rows, then columns
                for (int a = 0; a < size; ++a)
                {
                    auto at = [&] (int b) { return pass == 0 ? get (b, a) : get (a, b); };
                    int run = 1;
                    for (int b = 1; b <= size; ++b)
                    {
                        if (b < size && at (b) == at (b - 1))
                        {
                            ++run;
                            continue;
                        }
                        if (run >= 5)
                            score += 3 + (run - 5);
                        run = 1;
                    }
                    // 1:1:3:1:1 with four light modules on one side (outside counts as light).
                    auto light = [&] (int b) { return b < 0 || b >= size || ! at (b); };
                    for (int b = -4; b < size; ++b)
                    {
                        static constexpr bool pattern[] { true, false, true, true, true, false, true };
                        bool core = true;
                        for (int k = 0; k < 7 && core; ++k)
                            core = (b + k >= 0 && b + k < size) && at (b + k) == pattern[k];
                        if (! core)
                            continue;
                        bool before = true, after = true;
                        for (int k = 1; k <= 4; ++k)
                        {
                            before = before && light (b - k);
                            after  = after  && light (b + 6 + k);
                        }
                        if (before || after)
                            score += 40;
                    }
                }

            for (int y = 0; y + 1 < size; ++y)
                for (int x = 0; x + 1 < size; ++x)
                {
                    const bool c = get (x, y);
                    if (c == get (x + 1, y) && c == get (x, y + 1) && c == get (x + 1, y + 1))
                        score += 3;
                }

            long darkCount = 0;
            for (const bool d : dark)
                darkCount += d ? 1 : 0;
            const long total = static_cast<long> (size) * size;
            const long percent = darkCount * 100 / total;
            score += 10 * (std::labs (percent - 50) / 5);
            return score;
        }

    private:
        size_t index (int x, int y) const { return static_cast<size_t> (y * size + x); }

        void drawFinder (int cx, int cy)
        {
            for (int dy = -4; dy <= 4; ++dy)
                for (int dx = -4; dx <= 4; ++dx)
                {
                    const int x = cx + dx, y = cy + dy;
                    if (x < 0 || x >= size || y < 0 || y >= size)
                        continue;
                    const int ring = std::max (std::abs (dx), std::abs (dy));   // 4 = the light separator
                    setFunction (x, y, ring != 2 && ring != 4);
                }
        }

        void drawAlignment (int cx, int cy)
        {
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx)
                    setFunction (cx + dx, cy + dy, std::max (std::abs (dx), std::abs (dy)) != 1);
        }
    };

    // The data codewords: mode, length, the bytes, terminator, padding.
    std::vector<std::uint8_t> dataStream (const std::string& bytes, int version)
    {
        std::vector<bool> bits;
        auto put = [&bits] (unsigned value, int count)
        {
            for (int i = count - 1; i >= 0; --i)
                bits.push_back (((value >> i) & 1) != 0);
        };
        put (0x4, 4);   // byte mode
        put (static_cast<unsigned> (bytes.size()), headerBits (version) - 4);
        for (const char c : bytes)
            put (static_cast<unsigned char> (c), 8);

        const size_t capacity = static_cast<size_t> (dataCodewords (version)) * 8;
        put (0, static_cast<int> (std::min<size_t> (4, capacity - bits.size())));
        while (bits.size() % 8 != 0)
            bits.push_back (false);

        std::vector<std::uint8_t> out;
        for (size_t i = 0; i < bits.size(); i += 8)
        {
            unsigned byte = 0;
            for (size_t j = 0; j < 8; ++j)
                byte = byte << 1 | (bits[i + j] ? 1u : 0u);
            out.push_back (static_cast<std::uint8_t> (byte));
        }
        for (std::uint8_t pad = 0xec; out.size() < static_cast<size_t> (dataCodewords (version)); pad ^= 0xec ^ 0x11)
            out.push_back (pad);
        return out;
    }

    // Split into blocks (the short ones first), add each block's error
    // correction, and interleave: data column by column, then the ECC.
    std::vector<std::uint8_t> withErrorCorrection (const std::vector<std::uint8_t>& data, int version)
    {
        const int blocks = kBlocks[static_cast<size_t> (version)];
        const int ecc = kEccPerBlock[static_cast<size_t> (version)];
        const int total = totalCodewords (version);
        const int longBlocks = total % blocks;
        const int shortData = total / blocks - ecc;
        const auto gen = generator (ecc);

        std::vector<std::vector<std::uint8_t>> dataBlocks, eccBlocks;
        size_t pos = 0;
        for (int b = 0; b < blocks; ++b)
        {
            const int length = shortData + (b >= blocks - longBlocks ? 1 : 0);
            dataBlocks.emplace_back (data.begin() + static_cast<std::ptrdiff_t> (pos),
                                     data.begin() + static_cast<std::ptrdiff_t> (pos + static_cast<size_t> (length)));
            pos += static_cast<size_t> (length);
            eccBlocks.push_back (remainder (dataBlocks.back(), gen));
        }

        std::vector<std::uint8_t> out;
        for (int i = 0; i <= shortData; ++i)
            for (const auto& block : dataBlocks)
                if (i < static_cast<int> (block.size()))
                    out.push_back (block[static_cast<size_t> (i)]);
        for (int i = 0; i < ecc; ++i)
            for (const auto& block : eccBlocks)
                out.push_back (block[static_cast<size_t> (i)]);
        return out;
    }

    QrCode encode (const std::string& bytes, int forcedMask)
    {
        int version = 1;
        while (version <= kMaxVersion
               && headerBits (version) + 8 * static_cast<int> (bytes.size()) > dataCodewords (version) * 8)
            ++version;
        if (bytes.empty() || version > kMaxVersion)
            return {};

        Symbol symbol (version);
        symbol.drawFunctionPatterns();
        symbol.drawCodewords (withErrorCorrection (dataStream (bytes, version), version));

        int mask = forcedMask;
        if (mask < 0 || mask > 7)
        {
            long best = std::numeric_limits<long>::max();
            for (int m = 0; m < 8; ++m)
            {
                symbol.applyMask (m);
                symbol.drawFormat (m);
                if (const long p = symbol.penalty(); p < best)
                {
                    best = p;
                    mask = m;
                }
                symbol.applyMask (m);   // the mask is its own inverse
            }
        }
        symbol.applyMask (mask);
        symbol.drawFormat (mask);

        QrCode out;
        out.version = version;
        out.size = symbol.size;
        out.mask = mask;
        out.modules = symbol.dark;
        return out;
    }
}

QrCode encodeQr (const std::string& bytes)                 { return encode (bytes, -1); }
QrCode encodeQr (const std::string& bytes, int forcedMask) { return encode (bytes, forcedMask); }

}  // namespace boothcue
