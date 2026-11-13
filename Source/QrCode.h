#pragma once

#include <string>
#include <vector>

namespace boothcue
{

// A QR code for a short text (the songbook address for the band's phones).
// Plain C++, no JUCE, written for BoothCue (ISO/IEC 18004):
//
//   - byte mode (the text's UTF-8 bytes), error correction level M (15%)
//   - the smallest of versions 1-10 that fits: 21x21 to 57x57 modules,
//     up to 213 bytes; anything longer gives an empty code
//   - the mask with the lowest penalty score, as the standard describes
//
// The modules come without the quiet zone: draw four light modules around it.
struct QrCode
{
    int version = 0;              // 0: the text didn't fit
    int size = 0;                 // modules per side: 17 + 4 * version
    int mask = -1;                // 0-7
    std::vector<bool> modules;    // row by row, true = dark

    bool empty() const                 { return size == 0; }
    bool dark (int x, int y) const     { return modules[static_cast<size_t> (y * size + x)]; }
};

QrCode encodeQr (const std::string& bytes);

// The largest number of bytes encodeQr takes.
constexpr int qrMaxBytes = 213;

// For the checks: the code with a given mask (0-7) instead of the best one.
QrCode encodeQr (const std::string& bytes, int forcedMask);

}  // namespace boothcue
