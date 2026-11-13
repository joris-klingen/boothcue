// qr-check: the QR encoder (Source/QrCode.h) against the standard.
//
// Every code is read back with a small decoder written here from the standard,
// separately from the encoder: format and version information (BCH), the
// function patterns, the data in the zig-zag, the blocks de-interleaved and
// each one checked with its Reed-Solomon syndromes, then the byte-mode text.
//
// With QR_CHECK_DUMP=<dir>: every code is also written as <n>.txt (one row of
// 0/1 per line, first line the text) for a check with another reader (on the
// Mac: CoreImage's QR detector).

#include "QrCode.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace boothcue;

namespace
{
    int failures = 0;

#define EXPECT(cond)                                                                   \
    do                                                                                 \
    {                                                                                  \
        if (! (cond))                                                                  \
        {                                                                              \
            std::printf ("qr-check: FAILED %s (line %d)\n", #cond, __LINE__);         \
            ++failures;                                                                \
        }                                                                              \
    } while (false)

    // ---- the reader --------------------------------------------------------------

    std::uint8_t mul (std::uint8_t a, std::uint8_t b)
    {
        std::uint8_t r = 0;
        while (b != 0)
        {
            if (b & 1)
                r ^= a;
            a = static_cast<std::uint8_t> ((a << 1) ^ ((a & 0x80) ? 0x1d : 0));
            b >>= 1;
        }
        return r;
    }

    // A BCH code word is valid when the generator divides it.
    bool bchValid (long word, int bits, long gen, int genDegree)
    {
        for (int i = bits - 1; i >= genDegree; --i)
            if ((word >> i) & 1)
                word ^= gen << (i - genDegree);
        return word == 0;
    }

    struct Read
    {
        bool ok = false;
        int mask = -1;
        std::string text;
    };

    Read readBack (const QrCode& q)
    {
        Read r;
        const int n = q.size, v = q.version;
        auto at = [&] (int x, int y) { return q.dark (x, y); };

        // Format information, both copies (the order of encodeQr's placement
        // follows the standard's figure 25).
        int f1 = 0, f2 = 0;
        for (int i = 0; i <= 5; ++i) f1 |= at (8, i) << i;
        f1 |= at (8, 7) << 6;
        f1 |= at (8, 8) << 7;
        f1 |= at (7, 8) << 8;
        for (int i = 9; i < 15; ++i) f1 |= at (14 - i, 8) << i;
        for (int i = 0; i < 8; ++i) f2 |= at (n - 1 - i, 8) << i;
        for (int i = 8; i < 15; ++i) f2 |= at (8, n - 15 + i) << i;
        if (f1 != f2 || ! bchValid (f1 ^ 0x5412, 15, 0x537, 10) || ! at (8, n - 8))
            return r;
        const int format = (f1 ^ 0x5412) >> 10;
        if ((format >> 3) != 0)   // level M
            return r;
        r.mask = format & 7;

        if (v >= 7)
        {
            long a = 0, b = 0;
            for (int i = 0; i < 18; ++i)
            {
                a |= static_cast<long> (at (n - 11 + i % 3, i / 3)) << i;
                b |= static_cast<long> (at (i / 3, n - 11 + i % 3)) << i;
            }
            if (a != b || (a >> 12) != v || ! bchValid (a, 18, 0x1f25, 12))
                return r;
        }

        // Which modules are function patterns.
        std::vector<bool> fn (static_cast<size_t> (n * n));
        auto mark = [&] (int x, int y) { if (x >= 0 && y >= 0 && x < n && y < n) fn[static_cast<size_t> (y * n + x)] = true; };
        for (int y = 0; y < 9; ++y)
            for (int x = 0; x < 9; ++x)
            {
                mark (x, y);                 // finder, separator and format, top left
                mark (n - 1 - x + 1, y);     // top right (8 wide)
                mark (x, n - 1 - y + 1);     // bottom left (8 high)
            }
        for (int i = 0; i < n; ++i) { mark (6, i); mark (i, 6); }
        if (v >= 2)
        {
            const int count = v / 7 + 2;
            const int step = (v * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
            std::vector<int> c { 6 };
            for (int i = 1, pos = n - 7 - (count - 2) * step; i < count; ++i, pos += step)
                c.push_back (pos);
            for (size_t i = 0; i < c.size(); ++i)
                for (size_t j = 0; j < c.size(); ++j)
                {
                    if ((i == 0 && j == 0) || (i == 0 && j == c.size() - 1) || (i == c.size() - 1 && j == 0))
                        continue;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx)
                            mark (c[i] + dx, c[j] + dy);
                }
        }
        if (v >= 7)
            for (int i = 0; i < 18; ++i) { mark (n - 11 + i % 3, i / 3); mark (i / 3, n - 11 + i % 3); }

        // The timing patterns and finders must be there.
        for (int i = 8; i < n - 8; ++i)
            if (at (6, i) != (i % 2 == 0) || at (i, 6) != (i % 2 == 0))
                return r;
        for (int i = 0; i < 7; ++i)
            if (! at (i, 0) || ! at (n - 1 - i, 0) || ! at (0, n - 1 - i))
                return r;

        // The bits in the zig-zag, unmasked.
        std::vector<std::uint8_t> words;
        int bitCount = 0, byte = 0;
        for (int right = n - 1; right >= 1; right -= 2)
        {
            if (right == 6)
                right = 5;
            for (int step = 0; step < n; ++step)
                for (int j = 0; j < 2; ++j)
                {
                    const int x = right - j;
                    const int y (((right + 1) & 2) == 0 ? n - 1 - step : step);
                    if (fn[static_cast<size_t> (y * n + x)])
                        continue;
                    const int m = r.mask;
                    const bool flip = m == 0 ? (x + y) % 2 == 0 : m == 1 ? y % 2 == 0 : m == 2 ? x % 3 == 0
                                    : m == 3 ? (x + y) % 3 == 0 : m == 4 ? (x / 3 + y / 2) % 2 == 0
                                    : m == 5 ? x * y % 2 + x * y % 3 == 0 : m == 6 ? (x * y % 2 + x * y % 3) % 2 == 0
                                    : ((x + y) % 2 + x * y % 3) % 2 == 0;
                    byte = byte << 1 | (at (x, y) != flip ? 1 : 0);
                    if (++bitCount % 8 == 0)
                    {
                        words.push_back (static_cast<std::uint8_t> (byte));
                        byte = 0;
                    }
                }
        }

        // Blocks for level M (the standard's table 9), de-interleaved.
        static const int eccPer[] { 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
        static const int blockCount[] { 0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5 };
        const int blocks = blockCount[v], ecc = eccPer[v];
        const int total = static_cast<int> (words.size());
        const int longBlocks = total % blocks, shortData = total / blocks - ecc;
        std::vector<std::vector<std::uint8_t>> data (static_cast<size_t> (blocks)), full (static_cast<size_t> (blocks));
        size_t k = 0;
        for (int i = 0; i <= shortData; ++i)
            for (int b = 0; b < blocks; ++b)
                if (i < shortData || b >= blocks - longBlocks)
                    data[static_cast<size_t> (b)].push_back (words[k++]);
        for (int b = 0; b < blocks; ++b)
            full[static_cast<size_t> (b)] = data[static_cast<size_t> (b)];
        for (int i = 0; i < ecc; ++i)
            for (int b = 0; b < blocks; ++b)
                full[static_cast<size_t> (b)].push_back (words[k++]);

        // Each block, read as a polynomial, is zero at a^0 ... a^(ecc-1).
        std::uint8_t root = 1;
        for (int i = 0; i < ecc; ++i, root = mul (root, 2))
            for (const auto& block : full)
            {
                std::uint8_t s = 0;
                for (const auto c : block)
                    s = static_cast<std::uint8_t> (mul (s, root) ^ c);
                if (s != 0)
                    return r;
            }

        // Byte mode: 0100, an 8-bit length (versions 1-9; 16 bits from 10), the bytes.
        std::vector<bool> bits;
        for (const auto& block : data)
            for (const auto c : block)
                for (int i = 7; i >= 0; --i)
                    bits.push_back (((c >> i) & 1) != 0);
        size_t pos = 0;
        auto take = [&] (int count) { int value = 0; while (count-- > 0) value = value << 1 | (bits[pos++] ? 1 : 0); return value; };
        if (take (4) != 4)
            return r;
        const int length = take (v <= 9 ? 8 : 16);
        if (pos + static_cast<size_t> (length) * 8 > bits.size())
            return r;
        for (int i = 0; i < length; ++i)
            r.text.push_back (static_cast<char> (take (8)));
        r.ok = true;
        return r;
    }

    int dumped = 0;

    void dump (const QrCode& q, const std::string& text)
    {
        const char* dir = std::getenv ("QR_CHECK_DUMP");
        if (dir == nullptr)
            return;
        std::ofstream out (std::string (dir) + "/" + std::to_string (++dumped) + ".txt");
        out << text << "\n";
        for (int y = 0; y < q.size; ++y)
        {
            for (int x = 0; x < q.size; ++x)
                out << (q.dark (x, y) ? '1' : '0');
            out << "\n";
        }
    }

    void roundTrip (const std::string& text, int mask = -1)
    {
        const auto q = mask < 0 ? encodeQr (text) : encodeQr (text, mask);
        EXPECT (! q.empty());
        if (q.empty())
            return;
        EXPECT (q.size == 17 + 4 * q.version && static_cast<int> (q.modules.size()) == q.size * q.size);
        const auto r = readBack (q);
        EXPECT (r.ok && r.text == text && r.mask == q.mask);
        if (! r.ok || r.text != text)
            std::printf ("qr-check:   version %d mask %d, %zu bytes: \"%.40s\"\n", q.version, q.mask, text.size(), text.c_str());
        dump (q, text);
    }
}

int main()
{
    // The version is the smallest that holds the bytes (level M, byte mode).
    const int capacity[] { 0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213 };
    for (int v = 1; v <= 10; ++v)
    {
        std::string text;
        for (int i = 0; i < capacity[v]; ++i)
            text.push_back (static_cast<char> ('a' + (i * 7 + v) % 26));
        EXPECT (encodeQr (text).version == v);
        roundTrip (text);
        if (v < 10)
        {
            EXPECT (encodeQr (text + "x").version == v + 1);
            roundTrip (text + "x");
        }
    }
    EXPECT (qrMaxBytes == capacity[10]);
    EXPECT (encodeQr (std::string (214, 'x')).empty());
    EXPECT (encodeQr ("").empty());

    // Addresses as the manager gives them, every mask, and bytes beyond ASCII.
    const std::string address = "http://192.168.1.20:8770/?key=AbCdEfGhIjKl";
    for (int m = 0; m < 8; ++m)
        roundTrip (address, m);
    roundTrip (address);
    roundTrip ("http://10.0.0.5:8770/?key=x-_9Zq0");
    roundTrip ("http://studio.local:8770/?key=Wl3n1vXq_yZ8");
    roundTrip ("Papa is even weg \xe2\x80\x94 caf\xc3\xa9");
    roundTrip (std::string ("\x00\xff\x80\x7f", 4));
    roundTrip ("A");

    // The mask chosen is the one the format information names.
    EXPECT (encodeQr (address).mask >= 0 && encodeQr (address).mask <= 7);

    if (failures == 0)
        std::printf ("qr-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
