#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>

namespace boothcue
{

// What the host tells us about the timeline, captured once per audio block.
struct TransportState
{
    bool   valid     = false;   // the host has given a position at least once
    bool   playing   = false;
    bool   recording = false;
    bool   looping   = false;
    double ppq       = 0.0;
    double bpm       = 120.0;
    int    sigNum    = 4;
    int    sigDen    = 4;
    double loopStart = 0.0;
    double loopEnd   = 0.0;
    bool   hasLastBarStart = false;
    double lastBarStart    = 0.0;
    int    loopPass  = 0;       // 1 on the first pass through the loop; 0 = not looping
};

// Counts passes through the host's loop. Runs on the audio thread: arithmetic
// only. A pass is counted when the position jumps back to the loop start while
// playing; starting playback, or changing or switching off the loop, starts over.
class LoopCounter
{
public:
    int update (bool playing, bool looping, double loopStart, double loopEnd, double ppq) noexcept
    {
        const bool active = playing && looping && loopEnd > loopStart;
        const bool sameLoop = std::abs (loopStart - lastStart) < 1.0e-6 && std::abs (loopEnd - lastEnd) < 1.0e-6;

        if (! active)
            pass = 0;
        else if (! wasActive || ! sameLoop)
            pass = 1;
        else if (ppq < lastPpq - 1.0e-6                       // went backwards...
                 && lastPpq > loopStart                       // ...from inside the loop...
                 && ppq < loopStart + (loopEnd - loopStart) * 0.5)   // ...to its first half
            ++pass;

        wasActive = active;
        lastStart = loopStart;
        lastEnd   = loopEnd;
        lastPpq   = ppq;
        return pass;
    }

private:
    bool wasActive = false;
    double lastStart = 0.0, lastEnd = 0.0, lastPpq = 0.0;
    int pass = 0;
};

// Lock-free hand-off of the TransportState from the audio thread (one writer)
// to the message thread (readers): a sequence counter around atomic fields, so
// a reader never sees half of one block and half of the next.
class SharedTransport
{
public:
    void write (const TransportState& s) noexcept   // audio thread
    {
        seq.fetch_add (1, std::memory_order_acq_rel);   // odd: write in progress
        flags.store ((s.valid ? 1u : 0u) | (s.playing ? 2u : 0u) | (s.recording ? 4u : 0u)
                         | (s.looping ? 8u : 0u) | (s.hasLastBarStart ? 16u : 0u),
                     std::memory_order_relaxed);
        ppq.store (s.ppq, std::memory_order_relaxed);
        bpm.store (s.bpm, std::memory_order_relaxed);
        sigNum.store (s.sigNum, std::memory_order_relaxed);
        sigDen.store (s.sigDen, std::memory_order_relaxed);
        loopStart.store (s.loopStart, std::memory_order_relaxed);
        loopEnd.store (s.loopEnd, std::memory_order_relaxed);
        lastBarStart.store (s.lastBarStart, std::memory_order_relaxed);
        loopPass.store (s.loopPass, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_acq_rel);
    }

    TransportState read() const noexcept            // any other thread
    {
        TransportState s;
        for (int attempt = 0; attempt < 1000; ++attempt)
        {
            const auto before = seq.load (std::memory_order_acquire);
            const auto f = flags.load (std::memory_order_relaxed);
            s.valid           = (f & 1u) != 0;
            s.playing         = (f & 2u) != 0;
            s.recording       = (f & 4u) != 0;
            s.looping         = (f & 8u) != 0;
            s.hasLastBarStart = (f & 16u) != 0;
            s.ppq          = ppq.load (std::memory_order_relaxed);
            s.bpm          = bpm.load (std::memory_order_relaxed);
            s.sigNum       = sigNum.load (std::memory_order_relaxed);
            s.sigDen       = sigDen.load (std::memory_order_relaxed);
            s.loopStart    = loopStart.load (std::memory_order_relaxed);
            s.loopEnd      = loopEnd.load (std::memory_order_relaxed);
            s.lastBarStart = lastBarStart.load (std::memory_order_relaxed);
            s.loopPass     = loopPass.load (std::memory_order_relaxed);
            if ((before & 1u) == 0 && seq.load (std::memory_order_acquire) == before)
                break;
        }
        return s;
    }

private:
    std::atomic<std::uint32_t> seq { 0 };
    std::atomic<std::uint32_t> flags { 0 };
    std::atomic<double> ppq { 0.0 }, bpm { 120.0 }, loopStart { 0.0 }, loopEnd { 0.0 }, lastBarStart { 0.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 }, loopPass { 0 };
};

}  // namespace boothcue
