// watch-check — the `set-watcher` CTest. Runs the real SetWatcher (fast poll)
// on copies of the fixtures in a temp folder, and changes them the way Live
// does when saving: new contents, a file caught mid-write, a file that
// disappears and comes back.
//
//   watch-check <fixtures-dir>

#include <juce_core/juce_core.h>

#include "SetWatcher.h"

#include <cstdio>
#include <functional>

using namespace boothcue;

namespace
{
    int failures = 0;

    void expect (bool ok, const char* what, int line)
    {
        if (! ok)
        {
            std::printf ("FAIL (line %d): %s\n", line, what);
            ++failures;
        }
    }
    #define EXPECT(cond) expect ((cond), #cond, __LINE__)

    // Waits up to timeoutMs for a report; false if none came.
    bool next (SetWatcher& w, SetWatcher::Report& r, int timeoutMs = 3000)
    {
        const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
        while (juce::Time::getMillisecondCounter() < end)
        {
            if (w.takeReport (r))
                return true;
            juce::Thread::sleep (5);
        }
        return false;
    }

    bool quiet (SetWatcher& w, int ms)
    {
        SetWatcher::Report r;
        return ! next (w, r, ms);
    }

    void put (const juce::File& fixture, const juce::File& target)
    {
        // A save, as far as the watcher can tell: new bytes, a new time.
        EXPECT (fixture.copyFileTo (target));
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: watch-check <fixtures-dir>\n");
        return 2;
    }
    const juce::File fixtures (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const auto parts     = fixtures.getChildFile ("parts.als");
    const auto fallback  = fixtures.getChildFile ("fallback.als");
    const auto truncated = fixtures.getChildFile ("truncated.als");
    EXPECT (parts.existsAsFile() && fallback.existsAsFile() && truncated.existsAsFile());

    const auto dir = juce::File::createTempFile ("boothcue-watch");
    EXPECT (dir.createDirectory());
    const auto song  = dir.getChildFile ("My Song.als");
    const auto other = dir.getChildFile ("Other.als");
    put (parts, song);
    put (fallback, other);

    {
        SetWatcher w (40);
        SetWatcher::Report r;

        // Nothing watched: nothing reported.
        EXPECT (quiet (w, 200));

        // Linking reads at once.
        w.watch (song);
        EXPECT (next (w, r));
        EXPECT (r.file == song && ! r.missing && r.result.ok);
        EXPECT (r.result.sections.size() == 8 && r.result.trackName == "Parts");
        EXPECT (w.getFile() == song);

        // Unchanged: no new reads.
        EXPECT (quiet (w, 300));

        // A save with new contents.
        juce::Thread::sleep (20);
        put (fallback, song);
        EXPECT (next (w, r));
        EXPECT (r.result.ok && r.result.sections.size() == 2 && r.result.usedFallback);
        EXPECT (quiet (w, 200));

        // Caught halfway through a save: reported as an error...
        juce::Thread::sleep (20);
        put (truncated, song);
        EXPECT (next (w, r));
        EXPECT (! r.missing && ! r.result.ok && ! r.result.error.empty());
        // ...and the finished save is read again.
        juce::Thread::sleep (20);
        put (parts, song);
        EXPECT (next (w, r));
        EXPECT (r.result.ok && r.result.sections.size() == 8);

        // Overwritten in place with the same bytes but a new time: read again.
        juce::Thread::sleep (20);
        EXPECT (song.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5)));
        EXPECT (next (w, r));
        EXPECT (r.result.ok && r.result.sections.size() == 8);

        // A forced re-read of an unchanged file.
        EXPECT (quiet (w, 150));
        w.rescan();
        EXPECT (next (w, r));
        EXPECT (r.result.ok);

        // Gone (moved, or the set lives on another machine): reported once.
        EXPECT (song.deleteFile());
        EXPECT (next (w, r));
        EXPECT (r.missing && r.file == song);
        EXPECT (quiet (w, 250));
        // Back again: read at once.
        put (parts, song);
        EXPECT (next (w, r));
        EXPECT (! r.missing && r.result.ok && r.result.sections.size() == 8);

        // Re-pointed to another set: only that one is reported from now on.
        w.watch (other);
        EXPECT (next (w, r));
        EXPECT (r.file == other && r.result.ok && r.result.sections.size() == 2);
        juce::Thread::sleep (20);
        put (fallback, song);   // the old file changing doesn't matter any more
        EXPECT (quiet (w, 250));

        // Stopped.
        w.watch ({});
        juce::Thread::sleep (20);
        put (parts, other);
        EXPECT (quiet (w, 250));
        EXPECT (w.getFile() == juce::File());

        // Linked to a file that doesn't exist (yet).
        w.watch (dir.getChildFile ("Not there.als"));
        EXPECT (next (w, r));
        EXPECT (r.missing);
    }   // the destructor stops the thread

    dir.deleteRecursively();

    if (failures == 0)
        std::printf ("watch-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
