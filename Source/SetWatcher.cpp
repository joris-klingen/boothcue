#include "SetWatcher.h"

namespace boothcue
{

SetWatcher::SetWatcher (int poll)
    : juce::Thread ("BoothCue set watcher"), pollMs (juce::jmax (10, poll))
{
    startThread (juce::Thread::Priority::low);
}

SetWatcher::~SetWatcher()
{
    signalThreadShouldExit();
    wake.signal();
    // A parse stops at the parts track (normally the 2nd), so even a big set
    // finishes quickly; the timeout is a safety net.
    stopThread (5000);
}

void SetWatcher::watch (const juce::File& f)
{
    {
        const juce::ScopedLock sl (lock);
        file = f;
        ++generation;
        forceRescan = false;
        hasPending = false;
    }
    wake.signal();
}

void SetWatcher::rescan()
{
    {
        const juce::ScopedLock sl (lock);
        forceRescan = true;
    }
    wake.signal();
}

juce::File SetWatcher::getFile() const
{
    const juce::ScopedLock sl (lock);
    return file;
}

bool SetWatcher::takeReport (Report& out)
{
    const juce::ScopedLock sl (lock);
    if (! hasPending)
        return false;
    out = pending;
    hasPending = false;
    return true;
}

void SetWatcher::run()
{
    juce::uint32 seenGeneration = 0;
    bool fresh = true;              // a new file: parse at once, without waiting for it to settle
    bool reportedMissing = false;

    juce::Time seenMod, parsedMod;
    juce::int64 seenSize = -1, parsedSize = -1;

    while (! threadShouldExit())
    {
        juce::File current;
        juce::uint32 gen = 0;
        bool force = false;
        {
            const juce::ScopedLock sl (lock);
            current = file;
            gen = generation;
            force = forceRescan;
            forceRescan = false;
        }

        if (gen != seenGeneration)
        {
            seenGeneration = gen;
            fresh = true;
            reportedMissing = false;
            seenSize = parsedSize = -1;
        }

        if (current != juce::File())
        {
            if (! current.existsAsFile())
            {
                if (! reportedMissing)
                {
                    Report r;
                    r.file = current;
                    r.missing = true;
                    const juce::ScopedLock sl (lock);
                    if (generation == gen)
                    {
                        pending = r;
                        hasPending = true;
                    }
                }
                reportedMissing = true;
                fresh = true;    // when it comes back, read it straight away
                seenSize = parsedSize = -1;
            }
            else
            {
                reportedMissing = false;
                const auto mod  = current.getLastModificationTime();
                const auto size = current.getSize();

                const bool changed = mod != parsedMod || size != parsedSize;
                const bool settled = mod == seenMod && size == seenSize;

                if (force || (changed && (fresh || settled)))
                {
                    Report r;
                    r.file = current;
                    r.modified = mod;
                    r.result = parseAlsFile (current.getFullPathName().toStdString());

                    parsedMod = mod;
                    parsedSize = size;
                    fresh = false;

                    const juce::ScopedLock sl (lock);
                    if (generation == gen)   // not re-pointed while we parsed
                    {
                        pending = r;
                        hasPending = true;
                    }
                }
                seenMod = mod;
                seenSize = size;
            }
        }

        wake.wait (pollMs);
    }
}

}  // namespace boothcue
