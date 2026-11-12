#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>

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
    double capturedMs = 0.0;    // when the audio thread took this (a millisecond clock), 0 = unknown
};

// Where the playhead is at `nowMs`: the captured position moved on by the time
// since it was taken (while playing; at most 250 ms, in case audio stalled),
// wrapping at the end of the host loop.
inline double positionAt (const TransportState& t, double nowMs) noexcept
{
    if (! t.valid || ! t.playing || t.capturedMs <= 0.0)
        return t.ppq;
    const double dt = std::min (std::max (nowMs - t.capturedMs, 0.0), 250.0);
    double p = t.ppq + dt / 1000.0 * t.bpm / 60.0;
    if (t.looping && t.loopEnd > t.loopStart && t.ppq < t.loopEnd && p >= t.loopEnd)
        p = t.loopStart + (p - t.loopEnd);
    return p;
}

// The song position a tap at `nowMs` stands for: the playhead then, minus the
// tapper's latency (people tap a little late, consistently), snapped to a grid
// of `snapBeats` (0 = no snap).
inline double tapPosition (const TransportState& t, double nowMs, double latencyMs, double snapBeats) noexcept
{
    double p = positionAt (t, nowMs);
    if (t.playing)
        p -= latencyMs / 1000.0 * t.bpm / 60.0;
    if (snapBeats > 0.0)
        p = std::round (p / snapBeats) * snapBeats;
    return p;
}

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
//
// Every load and store is sequentially consistent (the default order), on
// purpose. With relaxed field stores nothing orders them after the counter
// going odd, nor a reader's field loads before its second look at the
// counter: on a weakly ordered CPU (ARM: Apple silicon) a reader could then
// take fields from two blocks and still find the counter unchanged. In one
// total order that can't happen: an unchanged, even counter means no write
// started or ended between the two looks. The cost is a dozen atomic stores
// per audio block.
class SharedTransport
{
public:
    void write (const TransportState& s) noexcept   // audio thread
    {
        seq.fetch_add (1);   // odd: write in progress
        flags.store ((s.valid ? 1u : 0u) | (s.playing ? 2u : 0u) | (s.recording ? 4u : 0u)
                         | (s.looping ? 8u : 0u) | (s.hasLastBarStart ? 16u : 0u));
        ppq.store (s.ppq);
        bpm.store (s.bpm);
        sigNum.store (s.sigNum);
        sigDen.store (s.sigDen);
        loopStart.store (s.loopStart);
        loopEnd.store (s.loopEnd);
        lastBarStart.store (s.lastBarStart);
        loopPass.store (s.loopPass);
        capturedMs.store (s.capturedMs);
        seq.fetch_add (1);
    }

    TransportState read() const noexcept            // any other thread
    {
        // A read that overlaps a write is taken again. When the audio thread
        // is suspended in the middle of a write, that takes as long as the
        // suspension: spinning on would run out of attempts and hand back
        // fields of two blocks, so after a few tries the reader gives way
        // instead (it is never the audio thread). The limit only guards
        // against a writer that never comes back.
        TransportState s;
        for (int attempt = 0; attempt < 100000; ++attempt)
        {
            if (attempt >= 16)
                std::this_thread::yield();
            const auto before = seq.load();
            const auto f = flags.load();
            s.valid           = (f & 1u) != 0;
            s.playing         = (f & 2u) != 0;
            s.recording       = (f & 4u) != 0;
            s.looping         = (f & 8u) != 0;
            s.hasLastBarStart = (f & 16u) != 0;
            s.ppq          = ppq.load();
            s.bpm          = bpm.load();
            s.sigNum       = sigNum.load();
            s.sigDen       = sigDen.load();
            s.loopStart    = loopStart.load();
            s.loopEnd      = loopEnd.load();
            s.lastBarStart = lastBarStart.load();
            s.loopPass     = loopPass.load();
            s.capturedMs   = capturedMs.load();
            if ((before & 1u) == 0 && seq.load() == before)
                break;
        }
        return s;
    }

private:
    // The audio thread must never take a lock: these have to be real atomics.
    static_assert (std::atomic<double>::is_always_lock_free && std::atomic<std::uint32_t>::is_always_lock_free
                       && std::atomic<int>::is_always_lock_free,
                   "SharedTransport needs lock-free atomics");

    std::atomic<std::uint32_t> seq { 0 };
    std::atomic<std::uint32_t> flags { 0 };
    std::atomic<double> ppq { 0.0 }, bpm { 120.0 }, loopStart { 0.0 }, loopEnd { 0.0 }, lastBarStart { 0.0 };
    std::atomic<double> capturedMs { 0.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 }, loopPass { 0 };
};

}  // namespace boothcue
