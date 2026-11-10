#pragma once

#include <juce_core/juce_core.h>

#include "AlsParser.h"
#include "SetChords.h"

#include <atomic>

namespace boothcue
{

// Watches the linked Live set and re-reads its song structure whenever the file
// changes, i.e. on every save in Live. Everything slow (stat, gunzip, parse)
// runs on this object's own thread; the message thread only hands it a file and
// collects reports.
//
// A change is parsed once the file's size and modification time have held
// still for one poll, so a save in progress isn't read halfway. A parse that
// fails is reported, and the next change is tried again: the caller keeps the
// last good structure meanwhile.
class SetWatcher final : private juce::Thread
{
public:
    struct Report
    {
        juce::File file;
        bool missing = false;    // the file doesn't exist (moved, renamed, another machine)
        AlsResult result;        // valid when ! missing; result.ok says whether it parsed
                                 // (its MIDI notes are dropped after detection)
        SetChords chords;        // detected from the set's MIDI tracks
        juce::Time modified;     // the file's modification time that was parsed
    };

    explicit SetWatcher (int pollMs = 1000);
    ~SetWatcher() override;

    // Message thread. An empty File stops watching.
    void watch (const juce::File&);
    // Parse the current file again now, changed or not.
    void rescan();

    // Message thread: the newest report since the last call, if any. Older
    // unread reports are dropped: only the latest state of the file matters.
    bool takeReport (Report&);

    juce::File getFile() const;

    // The bar length chord detection uses (from the host's time signature).
    // Call rescan() after a change to detect again.
    void setBarBeats (double beats) { barBeats.store (beats > 0.0 ? beats : 4.0); }

private:
    void run() override;

    const int pollMs;
    mutable juce::CriticalSection lock;
    juce::File file;              // guarded by lock
    juce::uint32 generation = 0;  // guarded: bumped by watch()
    bool forceRescan = false;     // guarded
    Report pending;               // guarded
    bool hasPending = false;      // guarded
    juce::WaitableEvent wake;
    std::atomic<double> barBeats { 4.0 };
};

}  // namespace boothcue
