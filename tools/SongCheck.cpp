// song-check — the `song-logic` CTest. Drives the real Song, LyricImport and
// Transport code (no JUCE, no clock): lyric blocks linking to sections, cue
// placement, the current / next line, the count-in, bar/beat readout, loop
// passes, and tap / nudge editing.

#include "LyricImport.h"
#include "Prompter.h"
#include "Song.h"
#include "Transport.h"

#include <cmath>
#include <cstdio>
#include <string>

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

    bool near (double a, double b) { return std::abs (a - b) < 1e-6; }

    Section sec (const char* name, double start, double end) { return { name, start, end }; }

    const char* kLyrics =
        "\xEF\xBB\xBF" "stray line before any header\r\n"
        "[Verse 1]\r\n"
        "v1 a\r\n"
        "v1 b\r\n"
        "\r\n"
        "  [ Chorus ]  \r\n"
        "ch a\n"
        "ch b\n"
        "ch c\n"
        "ch d\n"
        "[verse 2]\n"
        "v2 a\n";

    // Intro 4 bars, Verse 1 8, Chorus 8, Verse 2 8, Chorus 8, (gap 2 bars), Outro 4.   4/4.
    Song makeSong()
    {
        Song s;
        s.setSections ({ sec ("Intro", 0, 16), sec ("Verse 1", 16, 48), sec ("Chorus", 48, 80),
                         sec ("Verse 2", 80, 112), sec ("Chorus", 112, 144), sec ("Outro", 152, 168) });
        s.setBlocks (parseLyrics (kLyrics));
        return s;
    }

    const std::string& textOf (const Song& s, int cue)
    {
        static const std::string none = "<none>";
        if (cue < 0)
            return none;
        const auto& c = s.cues()[static_cast<std::size_t> (cue)];
        return s.blocks()[static_cast<std::size_t> (c.block)].lines[static_cast<std::size_t> (c.line)].text;
    }

    void importTests()
    {
        const auto blocks = parseLyrics (kLyrics);
        EXPECT (blocks.size() == 4);
        if (blocks.size() != 4)
            return;
        EXPECT (blocks[0].header.empty() && blocks[0].lines.size() == 1);
        EXPECT (blocks[0].lines[0].text == "stray line before any header");   // BOM and CR stripped
        EXPECT (blocks[1].header == "Verse 1" && blocks[1].lines.size() == 2);
        EXPECT (blocks[2].header == "Chorus" && blocks[2].lines.size() == 4);
        EXPECT (blocks[3].header == "verse 2" && blocks[3].lines.size() == 1);
        EXPECT (! blocks[1].lines[0].timed());

        EXPECT (parseLyrics ("").empty());
        EXPECT (parseLyrics ("\n\n  \n").empty());
        EXPECT (parseLyrics ("[Empty]").size() == 1);
        EXPECT (parseLyrics ("[a] not a header").front().header.empty());

        // Text round trip.
        const auto again = parseLyrics (lyricsToText (blocks));
        EXPECT (again.size() == blocks.size());
        EXPECT (lyricsToText (again) == lyricsToText (blocks));

        // Editing the text keeps the taps of unchanged lines.
        auto old = parseLyrics ("[Chorus]\na\nb\n[Verse]\nx\n[Chorus]\nc\nd\n");
        old[0].lines[0].offset = 1.0;
        old[0].lines[1].offset = 2.0;
        old[1].lines[0].offset = 3.0;
        old[2].lines[1].offset = 4.0;
        auto edited = parseLyrics ("[Intro]\nnew\n[chorus]\na\nB changed\n[Verse]\nx\n[Chorus]\nc\nd\nextra\n");
        keepTimings (old, edited);
        EXPECT (! edited[0].lines[0].timed());
        EXPECT (near (edited[1].lines[0].offset, 1.0));   // header matched case-insensitively
        EXPECT (! edited[1].lines[1].timed());            // text changed
        EXPECT (near (edited[2].lines[0].offset, 3.0));
        EXPECT (! edited[3].lines[0].timed());            // 2nd [Chorus] block matched with the 2nd, not the 1st
        EXPECT (near (edited[3].lines[1].offset, 4.0));
        EXPECT (! edited[3].lines[2].timed());

        EXPECT (nameKey (" Verse-1 ") == "verse1");
        EXPECT (nameKey ("PRE CHORUS") == nameKey ("Pre-Chorus"));
        EXPECT (nameKey ("[]") .empty());
    }

    void linkingTests()
    {
        const auto s = makeSong();
        EXPECT (s.blockForSection (0) == -1);   // Intro: no lyrics
        EXPECT (s.blockForSection (1) == 1);
        EXPECT (s.blockForSection (2) == 2);
        EXPECT (s.blockForSection (3) == 3);    // "Verse 2" <-> [verse 2]
        EXPECT (s.blockForSection (4) == 2);    // one [Chorus] block serves both choruses
        EXPECT (s.blockForSection (5) == -1);
        EXPECT (s.blockForSection (99) == -1);
        EXPECT (s.firstSectionForBlock (2) == 2);
        EXPECT (s.firstSectionForBlock (0) == -1);   // the header-less block links to nothing
        EXPECT (s.cues().size() == 2 + 4 + 1 + 4);
        EXPECT (near (s.end(), 168));

        // Several [Chorus] blocks: nth block -> nth chorus, the last one reused.
        Song multi;
        multi.setSections ({ sec ("Chorus", 0, 16), sec ("Chorus", 16, 32), sec ("Chorus", 32, 48) });
        multi.setBlocks (parseLyrics ("[Chorus]\nfirst\n[Chorus]\nsecond\n"));
        EXPECT (multi.blockForSection (0) == 0);
        EXPECT (multi.blockForSection (1) == 1);
        EXPECT (multi.blockForSection (2) == 1);

        // Sections all called "Verse", blocks numbered.
        Song numberedBlocks;
        numberedBlocks.setSections ({ sec ("Verse", 0, 16), sec ("Chorus", 16, 32), sec ("Verse", 32, 48) });
        numberedBlocks.setBlocks (parseLyrics ("[Verse 1]\na\n[Verse 2]\nb\n"));
        EXPECT (numberedBlocks.blockForSection (0) == 0);
        EXPECT (numberedBlocks.blockForSection (1) == -1);
        EXPECT (numberedBlocks.blockForSection (2) == 1);

        // Sections numbered, blocks all called [Chorus].
        Song numberedSections;
        numberedSections.setSections ({ sec ("Chorus 1", 0, 16), sec ("Chorus 2", 16, 32), sec ("Chorus 3", 32, 48) });
        numberedSections.setBlocks (parseLyrics ("[Chorus]\na\n[Chorus]\nb\n"));
        EXPECT (numberedSections.blockForSection (0) == 0);
        EXPECT (numberedSections.blockForSection (1) == 1);
        EXPECT (numberedSections.blockForSection (2) == 1);

        // Sections given out of order are sorted.
        Song unsorted;
        unsorted.setSections ({ sec ("B", 16, 32), sec ("A", 0, 16) });
        EXPECT (unsorted.sections()[0].name == "A");
        EXPECT (near (unsorted.start(), 0));

        Song empty;
        EXPECT (empty.sectionAt (0) == -1 && empty.cueAt (0) == -1 && empty.nextCueAfter (0) == -1);
        EXPECT (near (empty.end(), 0));
    }

    void positionTests()
    {
        const auto s = makeSong();

        EXPECT (s.sectionAt (-1) == -1);
        EXPECT (s.sectionAt (0) == 0);
        EXPECT (s.sectionAt (15.999) == 0);
        EXPECT (s.sectionAt (16) == 1);
        EXPECT (s.sectionAt (146) == -1);   // the gap before the Outro
        EXPECT (s.sectionAt (152) == 5);
        EXPECT (s.sectionAt (168) == -1);
        EXPECT (s.nextSectionAfter (0) == 1);
        EXPECT (s.nextSectionAfter (146) == 5);
        EXPECT (s.nextSectionAfter (160) == -1);

        // Untimed lines are spread evenly: 2 lines over 8 bars, 4 lines over 8 bars.
        EXPECT (near (s.cues()[0].ppq, 16) && near (s.cues()[1].ppq, 32));
        EXPECT (near (s.cues()[2].ppq, 48) && near (s.cues()[3].ppq, 56));

        EXPECT (s.cueAt (10) == -1);                    // intro
        EXPECT (textOf (s, s.nextCueAfter (10)) == "v1 a");
        EXPECT (textOf (s, s.cueAt (16)) == "v1 a");    // exactly on the line start
        EXPECT (textOf (s, s.nextCueAfter (16)) == "v1 b");
        EXPECT (textOf (s, s.cueAt (47.9)) == "v1 b");
        EXPECT (textOf (s, s.cueAt (48)) == "ch a");
        EXPECT (textOf (s, s.cueAt (100)) == "v2 a");
        EXPECT (textOf (s, s.nextCueAfter (100)) == "ch a");   // 2nd chorus, same block
        EXPECT (s.cues()[static_cast<std::size_t> (s.nextCueAfter (100))].section == 4);
        EXPECT (textOf (s, s.cueAt (143)) == "ch d");
        EXPECT (s.cueAt (146) == -1);                   // the line is gone once its section is over
        EXPECT (s.cueAt (160) == -1);
        EXPECT (s.nextCueAfter (143) == -1);
    }

    void spreadTests()
    {
        LyricBlock b;
        b.lines.resize (5);
        auto o = spreadOffsets (b, 20);
        EXPECT (near (o[0], 0) && near (o[1], 4) && near (o[4], 16));

        // Tapped lines stay; the others share the gaps around them.
        b.lines[1].offset = 2;
        b.lines[4].offset = 14;
        o = spreadOffsets (b, 20);
        EXPECT (near (o[0], 0));
        EXPECT (near (o[1], 2));
        EXPECT (near (o[2], 6) && near (o[3], 10));
        EXPECT (near (o[4], 14));

        b.lines[4].offset = -1;
        o = spreadOffsets (b, 20);
        EXPECT (near (o[2], 6.5) && near (o[3], 11) && near (o[4], 15.5));

        // A tap beyond the end of a shorter section doesn't push later lines backwards.
        b.lines[1].offset = 30;
        o = spreadOffsets (b, 20);
        EXPECT (o[2] >= 30 - 1e-9 && o[4] >= o[2]);

        EXPECT (spreadOffsets (LyricBlock(), 16).empty());
    }

    void tapTests()
    {
        auto s = makeSong();

        // Tap the chorus lines during the FIRST chorus...
        EXPECT (s.stamp (2, 0, 49));
        EXPECT (s.stamp (2, 1, 57.5));
        EXPECT (s.stamp (2, 2, 66));
        EXPECT (s.stamp (2, 3, 73));
        EXPECT (near (s.blocks()[2].lines[1].offset, 9.5));
        // ...and the SECOND chorus follows, at the same offsets.
        EXPECT (textOf (s, s.cueAt (112.5)) == "v2 a" || s.cueAt (112.5) == -1);
        EXPECT (textOf (s, s.cueAt (113)) == "ch a");
        EXPECT (textOf (s, s.cueAt (121.4)) == "ch a");
        EXPECT (textOf (s, s.cueAt (121.5)) == "ch b");
        EXPECT (s.cues()[static_cast<std::size_t> (s.cueAt (121.5))].timed);

        // Tapping in the second chorus retimes the shared block too.
        EXPECT (s.stamp (2, 3, 112 + 26));
        EXPECT (near (s.blocks()[2].lines[3].offset, 26));

        // Not while the playhead is in a section that sings something else.
        EXPECT (! s.stamp (2, 0, 20));     // verse 1
        EXPECT (! s.stamp (2, 0, 5));      // intro
        EXPECT (! s.stamp (2, 0, 146));    // gap
        EXPECT (! s.stamp (2, 9, 50));     // no such line
        EXPECT (! s.stamp (7, 0, 50));     // no such block

        // Re-tapping a line earlier than a previous line un-times that neighbour.
        EXPECT (s.stamp (2, 2, 48 + 5));   // before line 1 (9.5)
        EXPECT (! s.blocks()[2].lines[1].timed());
        EXPECT (near (s.blocks()[2].lines[0].offset, 1));
        EXPECT (near (s.resolvedOffset (2, 1), 3));   // now spread between 1 and 5
        // ...and later than a following line.
        EXPECT (s.stamp (2, 0, 48 + 6));
        EXPECT (! s.blocks()[2].lines[2].timed());
        EXPECT (s.blocks()[2].lines[3].timed());

        // Nudge: moves, and stays between the tapped neighbours.
        s.clearAllTimings();
        EXPECT (s.stamp (2, 0, 50));
        EXPECT (s.stamp (2, 2, 60));
        EXPECT (s.nudge (2, 2, -0.5));
        EXPECT (near (s.blocks()[2].lines[2].offset, 11.5));
        EXPECT (s.nudge (2, 2, -100));
        EXPECT (near (s.blocks()[2].lines[2].offset, 2));    // stopped at line 0
        EXPECT (s.nudge (2, 0, +100));
        EXPECT (near (s.blocks()[2].lines[0].offset, 2));
        EXPECT (s.nudge (2, 0, -100));
        EXPECT (near (s.blocks()[2].lines[0].offset, 0));
        // Nudging an untimed line pins it where it was shown, then moves it.
        const double shown = s.resolvedOffset (2, 3);
        EXPECT (s.nudge (2, 3, 0.25));
        EXPECT (near (s.blocks()[2].lines[3].offset, shown + 0.25));
        EXPECT (! s.nudge (0, 0, 1));    // unlinked block
        EXPECT (near (s.resolvedOffset (0, 0), -1));

        s.clearTiming (2, 3);
        EXPECT (! s.blocks()[2].lines[3].timed());
        s.clearTiming (9, 9);   // ignored
        s.clearAllTimings();
        EXPECT (! s.blocks()[2].lines[0].timed());

        // A tap late in a long section falls outside a shorter one of the same name.
        Song two;
        two.setSections ({ sec ("Chorus", 0, 32), sec ("Chorus", 32, 48) });
        two.setBlocks (parseLyrics ("[Chorus]\na\nb\n"));
        EXPECT (two.stamp (0, 1, 24));
        EXPECT (two.cues().size() == 3);   // a, b in the long one; only a in the short one
    }

    void countdownTests()
    {
        const auto s = makeSong();
        const TimeSig sig;   // 4/4
        CountSettings c;     // 2 bars announced, entries after a 4-bar rest
        using K = Countdown::Kind;

        // First line of the song at beat 16: bars 1-4 are the intro.
        EXPECT (countdownAt (s, 0, sig, c).kind == K::none);      // 4 bars to go: not yet
        EXPECT (countdownAt (s, 3.9, sig, c).kind == K::none);
        auto d = countdownAt (s, 4, sig, c);                      // 3 bars to go -> "2 bars"
        EXPECT (d.kind == K::bars && d.value == 2);
        EXPECT (textOf (s, d.cue) == "v1 a");
        d = countdownAt (s, 7.99, sig, c);
        EXPECT (d.kind == K::bars && d.value == 2);
        d = countdownAt (s, 8, sig, c);                           // 2 bars to go -> "1"
        EXPECT (d.kind == K::bars && d.value == 1);
        d = countdownAt (s, 12, sig, c);                          // the last bar: 4-3-2-1
        EXPECT (d.kind == K::beats && d.value == 4);
        EXPECT (countdownAt (s, 12.99, sig, c).value == 4);
        EXPECT (countdownAt (s, 13, sig, c).value == 3);
        EXPECT (countdownAt (s, 14.5, sig, c).value == 2);
        d = countdownAt (s, 15.99, sig, c);
        EXPECT (d.kind == K::beats && d.value == 1);
        // On the line itself the count is over; the next line (4 bars later) is an entry too.
        EXPECT (countdownAt (s, 16, sig, c).kind == K::none);
        EXPECT (countdownAt (s, 20, sig, c).kind == K::bars);

        // Chorus lines are 2 bars apart: no count-in between them.
        EXPECT (countdownAt (s, 48, sig, c).kind == K::none);
        EXPECT (countdownAt (s, 55, sig, c).kind == K::none);
        // ...unless the entry gap is lowered.
        c.entryGapBars = 2;
        EXPECT (countdownAt (s, 55, sig, c).kind == K::beats);
        c.entryGapBars = 4;

        // Fewer / no announced bars, and switched off.
        c.countInBars = 1;
        EXPECT (countdownAt (s, 4, sig, c).kind == K::none);
        EXPECT (countdownAt (s, 8, sig, c).kind == K::bars);
        c.countInBars = 0;
        EXPECT (countdownAt (s, 8, sig, c).kind == K::none);
        EXPECT (countdownAt (s, 12, sig, c).kind == K::beats);
        c.enabled = false;
        EXPECT (countdownAt (s, 12, sig, c).kind == K::none);
        c = CountSettings();

        // After the last line, and without lyrics.
        EXPECT (countdownAt (s, 150, sig, c).kind == K::none);
        EXPECT (countdownAt (Song(), 0, sig, c).kind == K::none);

        // 6/8: a bar is 3 quarter notes, counted in six eighths.
        const TimeSig sixEight { 6, 8 };
        EXPECT (near (sixEight.barLength(), 3) && near (sixEight.beatLength(), 0.5));
        d = countdownAt (s, 13, sixEight, c);
        EXPECT (d.kind == K::beats && d.value == 6);
        EXPECT (countdownAt (s, 15.6, sixEight, c).value == 1);
        d = countdownAt (s, 9.5, sixEight, c);      // 6.5 beats = 2.17 bars -> "2 bars"
        EXPECT (d.kind == K::bars && d.value == 2);
        // 3/4
        d = countdownAt (s, 13, TimeSig { 3, 4 }, c);
        EXPECT (d.kind == K::beats && d.value == 3);
    }

    void barBeatTests()
    {
        const TimeSig sig;
        EXPECT (barBeatAt (0, sig).bar == 1 && barBeatAt (0, sig).beat == 1);
        EXPECT (barBeatAt (3.99, sig).bar == 1 && barBeatAt (3.99, sig).beat == 4);
        EXPECT (barBeatAt (4, sig).bar == 2 && barBeatAt (4, sig).beat == 1);
        EXPECT (barBeatAt (17.5, sig).bar == 5 && barBeatAt (17.5, sig).beat == 2);
        // Live's count-in runs at negative positions: bar 0, counting up.
        EXPECT (barBeatAt (-4, sig).bar == 0 && barBeatAt (-4, sig).beat == 1);
        EXPECT (barBeatAt (-0.5, sig).bar == 0 && barBeatAt (-0.5, sig).beat == 4);

        const TimeSig sixEight { 6, 8 };
        EXPECT (barBeatAt (3, sixEight).bar == 2 && barBeatAt (3, sixEight).beat == 1);
        EXPECT (barBeatAt (4.25, sixEight).bar == 2 && barBeatAt (4.25, sixEight).beat == 3);

        // The host's own bar start wins when given.
        EXPECT (barBeatAt (17.5, sig, true, 16).bar == 5 && barBeatAt (17.5, sig, true, 16).beat == 2);
        // Never a beat outside the bar, whatever the host says.
        EXPECT (barBeatAt (30, sig, true, 16).beat == 4);
        EXPECT (barBeatAt (0, TimeSig { 0, 0 }).bar == 1);   // nonsense signature: treated as 4/4
    }

    void loopTests()
    {
        LoopCounter loop;
        // Not playing, not looping.
        EXPECT (loop.update (false, true, 16, 32, 16) == 0);
        EXPECT (loop.update (true, false, 16, 32, 16) == 0);
        // First pass.
        EXPECT (loop.update (true, true, 16, 32, 16) == 1);
        EXPECT (loop.update (true, true, 16, 32, 24) == 1);
        EXPECT (loop.update (true, true, 16, 32, 31.9) == 1);
        // Wrap: second pass, counted once.
        EXPECT (loop.update (true, true, 16, 32, 16.1) == 2);
        EXPECT (loop.update (true, true, 16, 32, 20) == 2);
        // A small backwards jump inside the second half isn't a pass.
        EXPECT (loop.update (true, true, 16, 32, 30) == 2);
        EXPECT (loop.update (true, true, 16, 32, 28) == 2);
        EXPECT (loop.update (true, true, 16, 32, 31) == 2);
        EXPECT (loop.update (true, true, 16, 32, 16) == 3);
        // Stop and start again: back to pass 1.
        EXPECT (loop.update (false, true, 16, 32, 20) == 0);
        EXPECT (loop.update (true, true, 16, 32, 16) == 1);
        EXPECT (loop.update (true, true, 16, 32, 31) == 1);
        EXPECT (loop.update (true, true, 16, 32, 16) == 2);
        // Moving the loop starts over.
        EXPECT (loop.update (true, true, 16, 48, 17) == 1);
        // Playing before the loop (pre-roll) and then entering it is still pass 1.
        LoopCounter pre;
        EXPECT (pre.update (true, true, 16, 32, 12) == 1);
        EXPECT (pre.update (true, true, 16, 32, 15.9) == 1);
        EXPECT (pre.update (true, true, 16, 32, 16.2) == 1);
        EXPECT (pre.update (true, true, 16, 32, 31.9) == 1);
        EXPECT (pre.update (true, true, 16, 32, 16.0) == 2);
        // A loop with no length is no loop.
        EXPECT (pre.update (true, true, 16, 16, 16) == 0);

        // The shared snapshot hands every field over unchanged.
        SharedTransport shared;
        EXPECT (! shared.read().valid);
        TransportState t;
        t.valid = t.playing = t.looping = t.hasLastBarStart = true;
        t.recording = false;
        t.ppq = 33.25; t.bpm = 96.5; t.sigNum = 6; t.sigDen = 8;
        t.loopStart = 16; t.loopEnd = 48; t.lastBarStart = 33; t.loopPass = 3;
        shared.write (t);
        const auto r = shared.read();
        EXPECT (r.valid && r.playing && r.looping && r.hasLastBarStart && ! r.recording);
        EXPECT (near (r.ppq, 33.25) && near (r.bpm, 96.5) && r.sigNum == 6 && r.sigDen == 8);
        EXPECT (near (r.loopStart, 16) && near (r.loopEnd, 48) && near (r.lastBarStart, 33) && r.loopPass == 3);
    }
    TransportState at (double ppq, bool playing = true)
    {
        TransportState t;
        t.valid = true;
        t.playing = playing;
        t.ppq = ppq;
        return t;
    }

    void sceneTests()
    {
        const auto song = makeSong();
        DisplaySettings ds;

        EXPECT (describeDistance (12, TimeSig()) == "3 bars");
        EXPECT (describeDistance (9, TimeSig()) == "3 bars");      // rounded up
        EXPECT (describeDistance (8, TimeSig()) == "2 bars");
        EXPECT (describeDistance (5, TimeSig()) == "2 bars");
        EXPECT (describeDistance (4, TimeSig()) == "1 bar");
        EXPECT (describeDistance (3.5, TimeSig()) == "4 beats");
        EXPECT (describeDistance (1, TimeSig()) == "1 beat");
        EXPECT (describeDistance (0.2, TimeSig()) == "1 beat");
        EXPECT (describeDistance (1.5, TimeSig { 6, 8 }) == "3 beats");

        // No host position: the song is still known (for the timeline), nothing else.
        auto s = buildScene (song, TransportState(), ds);
        EXPECT (! s.hasPosition && s.hasSong && s.hasLyrics);
        EXPECT (s.section == -1 && s.currentCue == -1 && s.countdown.kind == Countdown::Kind::none);
        EXPECT (near (s.timelineStart, 0) && near (s.timelineEnd, 168));

        // Intro, 3 bars before the first line: counting in, next line known.
        s = buildScene (song, at (4), ds);
        EXPECT (s.hasPosition && s.playing && ! s.recording);
        EXPECT (s.section == 0 && s.nextSection == 1 && near (s.beatsToNextSection, 12));
        EXPECT (s.currentLine.empty() && s.nextLine == "v1 a" && near (s.beatsToNextLine, 12));
        EXPECT (s.countdown.kind == Countdown::Kind::bars && s.countdown.value == 2);
        EXPECT (s.barBeat.bar == 2 && s.barBeat.beat == 1);

        // Mid chorus.
        s = buildScene (song, at (57), ds);
        EXPECT (s.section == 2 && s.currentLine == "ch b" && s.nextLine == "ch c");
        EXPECT (s.countdown.kind == Countdown::Kind::none);

        // In the gap before the Outro: no section, no line, nothing to come.
        s = buildScene (song, at (146), ds);
        EXPECT (s.section == -1 && s.nextSection == 5 && s.currentCue == -1 && s.nextCue == -1);
        EXPECT (s.nextLine.empty());

        // Lyrics switched off: no line, no count-in; the structure is still there.
        ds.showLyrics = false;
        s = buildScene (song, at (12), ds);
        EXPECT (s.section == 0 && s.currentLine.empty() && s.nextLine.empty());
        EXPECT (s.countdown.kind == Countdown::Kind::none);
        ds.showLyrics = true;

        // Stopped: the position is still followed (Live reports it while stopped).
        s = buildScene (song, at (57, false), ds);
        EXPECT (! s.playing && s.currentLine == "ch b");

        // The host loop and its passes, and the time signature.
        auto t = at (60);
        t.looping = true; t.loopStart = 48; t.loopEnd = 80; t.loopPass = 3;
        t.recording = true; t.sigNum = 3; t.sigDen = 4;
        s = buildScene (song, t, ds);
        EXPECT (s.loopShown && s.looping && s.loopPass == 3 && s.recording);
        EXPECT (s.sig.num == 3 && s.barBeat.bar == 21);
        t.looping = false;
        s = buildScene (song, t, ds);
        EXPECT (s.loopShown && ! s.looping && s.loopPass == 0);   // the range stays visible, greyed
        t.loopEnd = t.loopStart;
        EXPECT (! buildScene (song, t, ds).loopShown);

        // No set linked, no lyrics.
        s = buildScene (Song(), at (10), ds);
        EXPECT (! s.hasSong && ! s.hasLyrics && s.section == -1 && s.nextLine.empty());

        // A structure without lyrics.
        Song bare;
        bare.setSections ({ sec ("Intro", 0, 16), sec ("Verse", 16, 48) });
        s = buildScene (bare, at (10), ds);
        EXPECT (s.hasSong && ! s.hasLyrics && s.section == 0 && s.nextSection == 1);

        // A song that starts before bar 1 widens the timeline; a nonsense signature reads as 4/4.
        Song early;
        early.setSections ({ sec ("Pickup", -4, 0), sec ("Verse", 0, 16) });
        t = at (0);
        t.sigNum = 0; t.sigDen = 0;
        s = buildScene (early, t, ds);
        EXPECT (near (s.timelineStart, -4) && near (s.timelineEnd, 16));
        EXPECT (s.sig.num == 4 && s.sig.den == 4);
    }
}

int main()
{
    importTests();
    linkingTests();
    positionTests();
    spreadTests();
    tapTests();
    countdownTests();
    barBeatTests();
    loopTests();
    sceneTests();

    if (failures == 0)
        std::printf ("song-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
