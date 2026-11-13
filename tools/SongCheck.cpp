// song-check — the `song-logic` CTest. Drives the real Song, LyricImport and
// Transport code (no JUCE, no clock): lyric blocks linking to sections, cue
// placement, the current / next line, the count-in, bar/beat readout, loop
// passes, and tap / nudge editing.

#include "ChordChart.h"
#include "LyricImport.h"
#include "Prompter.h"
#include "Song.h"
#include "Transport.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

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
        EXPECT (blocks[3].header == "Verse 2" && blocks[3].lines.size() == 1);   // headers capitalised
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

        // Inserting, removing and changing lines keeps the other lines' taps.
        auto taps = parseLyrics ("[Verse]\none\ntwo\nthree\nfour\n");
        for (std::size_t l = 0; l < 4; ++l)
            taps[0].lines[l].offset = static_cast<double> (l) * 4.0;
        auto moved = parseLyrics ("[Verse]\nnew first\none\nthree\nfour (changed)\nfour\n");
        keepTimings (taps, moved);
        EXPECT (! moved[0].lines[0].timed());
        EXPECT (near (moved[0].lines[1].offset, 0));    // one
        EXPECT (near (moved[0].lines[2].offset, 8));    // three (two was removed)
        EXPECT (! moved[0].lines[3].timed());
        EXPECT (near (moved[0].lines[4].offset, 12));   // four
        // A repeated line pairs up in order.
        auto rep = parseLyrics ("[C]\nla\nla\n");
        rep[0].lines[0].offset = 1; rep[0].lines[1].offset = 2;
        auto rep2 = parseLyrics ("[C]\nla\nx\nla\n");
        keepTimings (rep, rep2);
        EXPECT (near (rep2[0].lines[0].offset, 1) && ! rep2[0].lines[1].timed() && near (rep2[0].lines[2].offset, 2));

        // Saved starts laid over a text: only lines with a start are taken, taps and "@" alike.
        auto text = parseLyrics ("[Verse]\none\ntwo\nthree\n@9.1.1 four\n");
        auto saved = parseLyrics ("[Verse]\n@5.1.1 one\ntwo\nthree\nfour\n");
        saved[0].lines[2].offset = 6.0;
        keepStarts (saved, text);
        EXPECT (text[0].lines[0].hasAt && text[0].lines[0].atBar == 5 && text[0].lines[0].text == "one");
        EXPECT (! text[0].lines[1].timed() && ! text[0].lines[1].hasAt);
        EXPECT (near (text[0].lines[2].offset, 6.0));
        EXPECT (text[0].lines[3].hasAt && text[0].lines[3].atBar == 9);   // no saved start: its own stays

        EXPECT (sectionKind ("Intro") == SectionKind::intro && sectionKind ("Verse 2") == SectionKind::verse);
        EXPECT (sectionKind ("Pre-Chorus") == SectionKind::prechorus && sectionKind ("pre") == SectionKind::prechorus);
        EXPECT (sectionKind ("Chorus 3") == SectionKind::chorus && sectionKind ("Refrein") == SectionKind::chorus);
        EXPECT (sectionKind ("Brug") == SectionKind::bridge && sectionKind ("Middle 8") == SectionKind::bridge);
        EXPECT (sectionKind ("Outro") == SectionKind::outro && sectionKind ("Slot") == SectionKind::outro);
        EXPECT (sectionKind ("Solo") == SectionKind::other && sectionKind ("") == SectionKind::other);
        EXPECT (sectionKind ("Couplet 1") == SectionKind::verse);
        EXPECT (sectionKind ("intro v2") == SectionKind::intro && sectionKind ("verse b") == SectionKind::verse);
        EXPECT (sectionKind ("Pre chorus 2") == SectionKind::prechorus && sectionKind ("Interlude") == SectionKind::other);

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
        // A nonsense position (hosts do hand them over) is still a number that can be shown.
        EXPECT (barBeatAt (std::nan (""), sig).bar == 1 && barBeatAt (std::nan (""), sig).beat == 1);
        EXPECT (barBeatAt (1.0e300, sig).bar == 1000000001 && barBeatAt (-1.0e300, sig).bar == -999999999);
        EXPECT (formatPosition (std::nan (""), sig) == "1.1.1");
        EXPECT (describeDistance (1.0e300, sig) == "250000000 bars" && describeDistance (std::nan (""), sig) == "1 beat");

        EXPECT (formatPosition (0, sig) == "1.1.1");
        EXPECT (formatPosition (57.5, sig) == "15.2.3");
        EXPECT (formatPosition (57.74, sig) == "15.2.3");
        EXPECT (formatPosition (57.75, sig) == "15.2.4");
        EXPECT (formatPosition (4.25, sixEight) == "2.3.2");   // 6/8: beats are eighths, 2 sixteenths each
        EXPECT (formatPosition (4.0, sixEight) == "2.3.1");
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

        // One thread writing (as the audio thread does), one reading: a
        // snapshot is always one block's, never fields of two.
        SharedTransport busy;
        std::atomic<bool> stop { false };
        std::thread writer ([&busy, &stop]
        {
            TransportState w;
            w.valid = true;
            for (int i = 1; ! stop.load(); ++i)
            {
                w.ppq = i; w.bpm = i + 1; w.loopStart = i + 2; w.loopEnd = i + 3; w.lastBarStart = i + 4;
                w.capturedMs = i + 5; w.sigNum = i; w.sigDen = i + 1; w.loopPass = i + 2;
                w.playing = w.looping = (i & 1) != 0;
                w.recording = w.hasLastBarStart = (i & 1) == 0;
                busy.write (w);   // without a pause: far more often than any audio thread
            }
        });
        int torn = 0;
        for (int n = 0; n < 2000000; ++n)
        {
            const auto s = busy.read();
            if (! s.valid)
                continue;   // nothing written yet
            const int i = s.sigNum;
            const bool odd = (i & 1) != 0;
            const bool whole = s.ppq == i && s.bpm == i + 1 && s.loopStart == i + 2 && s.loopEnd == i + 3
                               && s.lastBarStart == i + 4 && s.capturedMs == i + 5 && s.sigDen == i + 1 && s.loopPass == i + 2
                               && s.playing == odd && s.looping == odd && s.recording == ! odd && s.hasLastBarStart == ! odd;
            torn += whole ? 0 : 1;
        }
        stop = true;
        writer.join();
        EXPECT (torn == 0);
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

        // Read ahead (leadBars, 1 bar by default): "ch c" starts at 64 and takes
        // the big position a bar before, with the run-up to its first word.
        s = buildScene (song, at (59.9), ds);
        EXPECT (s.currentLine == "ch b" && ! s.lineAhead);
        s = buildScene (song, at (61), ds);
        EXPECT (s.lineAhead && s.currentLine == "ch c" && s.nextCue == s.currentCue + 1);
        EXPECT (s.laterLine == textOf (song, s.nextCue + 1) && ! s.laterLine.empty() && s.laterLine != s.nextLine);
        EXPECT (near (s.beatsToLine, 3) && near (s.lineRunUp, 0.25) && near (s.beatsToNextLine, 11));
        s = buildScene (song, at (64), ds);
        EXPECT (! s.lineAhead && s.currentLine == "ch c");
        // Never more than half the time since the line before it started (8 beats here).
        ds.leadBars = 2.0;
        s = buildScene (song, at (59.9), ds);
        EXPECT (s.currentLine == "ch b" && ! s.lineAhead);
        s = buildScene (song, at (62), ds);
        EXPECT (s.lineAhead && s.currentLine == "ch c" && near (s.lineRunUp, 0.5));
        // Off: a line comes at its first word.
        ds.leadBars = 0.0;
        s = buildScene (song, at (63.5), ds);
        EXPECT (s.currentLine == "ch b" && ! s.lineAhead && near (s.lineRunUp, 0));
        ds.leadBars = 1.0;
        // An entry that is counted in is announced by the count, not shown ahead...
        s = buildScene (song, at (14), ds);
        EXPECT (s.countdown.kind == Countdown::Kind::beats && s.currentLine.empty() && ! s.lineAhead && s.nextLine == "v1 a");
        // ...unless the count-in is off.
        ds.count.enabled = false;
        s = buildScene (song, at (14), ds);
        EXPECT (s.lineAhead && s.currentLine == "v1 a" && near (s.beatsToLine, 2) && near (s.lineRunUp, 0.5));
        s = buildScene (song, at (4), ds);
        EXPECT (! s.lineAhead && s.currentLine.empty() && s.nextLine == "v1 a");
        ds.count.enabled = true;

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
    void gridTests()
    {
        LyricBlock b;
        b.lines.resize (4);
        // 4 lines over 8 bars of 4/4: bars 1, 3, 5, 7.
        auto o = spreadOffsets (b, 32, 0, 4);
        EXPECT (near (o[0], 0) && near (o[1], 8) && near (o[2], 16) && near (o[3], 24));
        // 3 lines over 8 bars: the even spread (0, 10.67, 21.33) lands on bars.
        b.lines.resize (3);
        o = spreadOffsets (b, 32, 0, 4);
        EXPECT (near (o[0], 0) && near (o[1], 12) && near (o[2], 20));
        // 5 lines over 2 bars: bars and half bars collide, quarter bars (beats) don't.
        b.lines.resize (5);
        o = spreadOffsets (b, 8, 0, 4);
        EXPECT (near (o[0], 0) && near (o[1], 2) && near (o[2], 3) && near (o[3], 5) && near (o[4], 6));
        // More lines than quarter bars: left unsnapped.
        b.lines.resize (9);
        o = spreadOffsets (b, 8, 0, 4);
        EXPECT (near (o[1], 8.0 / 9.0));
        // The grid is the song's: a section starting on beat 2 of bar 5 puts
        // its later lines on bar lines (its first line stays on its start).
        b.lines.resize (3);
        o = spreadOffsets (b, 31, 17, 4);
        EXPECT (near (o[0], 0));
        EXPECT (near (17 + o[1], 28) && near (17 + o[2], 36));
        // After a tapped line, strictly after it; before the next tapped line.
        b.lines.resize (4);
        b.lines[1].offset = 5;      // tapped just after bar 2
        b.lines[3].offset = 20;
        o = spreadOffsets (b, 32, 0, 4);
        EXPECT (near (o[0], 0) && near (o[1], 5) && near (o[3], 20));
        EXPECT (o[2] > 5 && o[2] < 20 && near (std::fmod (o[2], 4), 0));   // 12.5 -> bar 12
        // 6/8 (a bar is 3 quarter notes).
        LyricBlock six;
        six.lines.resize (4);
        o = spreadOffsets (six, 24, 0, 3);
        EXPECT (near (o[1], 6) && near (o[2], 12) && near (o[3], 18));

        // Through the Song: setting the bar length moves the cues.
        Song s;
        s.setSections ({ sec ("Verse", 0, 32) });
        s.setBlocks (parseLyrics ("[Verse]\na\nb\nc\n"));
        EXPECT (near (s.cues()[1].ppq, 32.0 / 3.0));
        s.setBarLength (4);
        EXPECT (near (s.barLength(), 4) && near (s.cues()[1].ppq, 12) && near (s.cues()[2].ppq, 20));
        EXPECT (near (s.resolvedOffset (0, 1), 12));
        s.setBarLength (0);
        EXPECT (near (s.cues()[1].ppq, 32.0 / 3.0));
    }

    void tapCursorTests()
    {
        auto s = makeSong();   // Intro, Verse 1 (2 lines), Chorus (4), Verse 2 (1), Chorus, gap, Outro
        TapCursor c;

        auto r = tapAt (s, c, 5);
        EXPECT (! r.ok && r.error.find ("Intro") != std::string::npos);
        r = tapAt (s, c, 146);
        EXPECT (! r.ok);   // the gap

        // Verse 1: the cursor starts at its first line and moves on.
        r = tapAt (s, c, 17);
        EXPECT (r.ok && r.block == 1 && r.line == 0 && r.section == 1);
        EXPECT (c.block == 1 && c.line == 1);
        r = tapAt (s, c, 33);
        EXPECT (r.ok && r.line == 1);
        EXPECT (near (s.blocks()[1].lines[0].offset, 1) && near (s.blocks()[1].lines[1].offset, 17));
        // One tap too many in the same section.
        r = tapAt (s, c, 40);
        EXPECT (! r.ok && c.line == 2);

        // Into the chorus: the cursor follows the playhead to that block.
        r = tapAt (s, c, 49);
        EXPECT (r.ok && r.block == 2 && r.line == 0);
        r = tapAt (s, c, 57);
        EXPECT (r.ok && r.line == 1);

        // Stopping halfway and picking it up in the 2nd chorus continues at line 2...
        r = tapAt (s, c, 112 + 17);
        EXPECT (r.ok && r.block == 2 && r.line == 2 && r.section == 4);
        r = tapAt (s, c, 112 + 25);
        EXPECT (r.ok && r.line == 3);
        // ...and a chorus already finished starts over in the next one.
        Song two;
        two.setSections ({ sec ("Chorus", 0, 16), sec ("Chorus", 16, 32) });
        two.setBlocks (parseLyrics ("[Chorus]\na\nb\n"));
        TapCursor t;
        EXPECT (tapAt (two, t, 0.5).ok && tapAt (two, t, 8.5).ok);
        EXPECT (! tapAt (two, t, 12).ok);              // done here
        r = tapAt (two, t, 16.25);
        EXPECT (r.ok && r.line == 0 && r.section == 1);
        EXPECT (near (two.blocks()[0].lines[0].offset, 0.25));

        // Selecting a line re-taps just that one.
        c = TapCursor { 2, 1, -1 };
        r = tapAt (s, c, 48 + 7);
        EXPECT (r.ok && r.block == 2 && r.line == 1 && near (s.blocks()[2].lines[1].offset, 7));

        // A selected line of another block: the playhead's block wins.
        c = TapCursor { 1, 0, -1 };
        r = tapAt (s, c, 48 + 2);
        EXPECT (r.ok && r.block == 2 && r.line == 0);

        // A block with no lines.
        Song empty;
        empty.setSections ({ sec ("Verse", 0, 16) });
        empty.setBlocks (parseLyrics ("[Verse]\n"));
        TapCursor e;
        EXPECT (! tapAt (empty, e, 1).ok);
    }

    void tapPositionTests()
    {
        TransportState t;
        t.valid = true;
        t.playing = true;
        t.ppq = 16.0;
        t.bpm = 120.0;          // 2 beats per second
        t.capturedMs = 1000.0;

        EXPECT (near (positionAt (t, 1000.0), 16.0));
        EXPECT (near (positionAt (t, 1100.0), 16.2));       // 100 ms later
        EXPECT (near (positionAt (t, 900.0), 16.0));        // never backwards
        EXPECT (near (positionAt (t, 5000.0), 16.5));       // at most 250 ms
        // Wraps at the end of the loop.
        t.looping = true; t.loopStart = 8; t.loopEnd = 16.1;
        EXPECT (near (positionAt (t, 1100.0), 8.1));
        t.looping = false;
        // Stopped, or no capture time: the position as it is.
        t.playing = false;
        EXPECT (near (positionAt (t, 1100.0), 16.0));
        t.playing = true;
        t.capturedMs = 0;
        EXPECT (near (positionAt (t, 1100.0), 16.0));
        t.capturedMs = 1000.0;

        // Latency: 50 ms at 120 BPM = 0.1 beat earlier.
        EXPECT (near (tapPosition (t, 1100.0, 50.0, 0.0), 16.1));
        // Snap to 16ths (0.25 beat) and 8ths.
        EXPECT (near (tapPosition (t, 1100.0, 50.0, 0.25), 16.0));
        EXPECT (near (tapPosition (t, 1140.0, 0.0, 0.25), 16.25));
        EXPECT (near (tapPosition (t, 1140.0, 0.0, 0.5), 16.5));
        // No latency correction while stopped (placing a line at the playhead).
        t.playing = false;
        EXPECT (near (tapPosition (t, 1100.0, 50.0, 0.0), 16.0));
    }
    // The import rules one by one, including what must NOT trigger them.
    void importRuleTests()
    {
        LyricImportReport r;

        // Headers without brackets, Dutch names, colons, "x2", inline first lines.
        auto b = parseLyrics ("verse 1\na\nRefrein:\nb\nBrug: c\nd\n[Chorus x2]\ne\n", &r);
        EXPECT (b.size() == 4);
        if (b.size() == 4)
        {
            EXPECT (b[0].header == "Verse 1" && b[1].header == "Refrein" && b[2].header == "Brug" && b[3].header == "Chorus");
            EXPECT (b[2].lines.size() == 2 && b[2].lines[0].text == "c");
            EXPECT (nameKey (b[1].header) == "chorus" && nameKey (b[2].header) == "bridge");
        }
        EXPECT (r.anyHeader && r.chordCharts.empty() && r.title.empty());

        // Lyric lines that only look like headers.
        b = parseLyrics ("Chorus girls dance all night\nIntro to the night\nSolo, so low\n");
        EXPECT (b.size() == 1 && b[0].header.empty() && b[0].lines.size() == 3);

        // A part with no lines of its own (instrumental) is kept; a part named
        // before, on its own, is a repeat marker.
        b = parseLyrics ("[Verse]\na\n[Solo]\n[Chorus]\nb\n\nChorus\n\n[Verse]\nc\n", &r);
        EXPECT (b.size() == 4 && b[1].header == "Solo" && b[1].lines.empty());
        EXPECT (r.repeatMarkers == 1);
        // ...but a repeated header with its lines right under it is a block.
        b = parseLyrics ("[Chorus]\na\n\n[Chorus]\na\n", &r);
        EXPECT (b.size() == 2 && r.repeatMarkers == 0);

        // Chord charts: kept aside, never sung. Pipes in a lyric are lyrics.
        b = parseLyrics ("Verse: ||: Am | | |Dm E(7b13) :||\n|  F  |Am/E  |Esus4  |E ||\n"
                         "(Roeren:) | Dm | C | G | Bb C ||\nZij zegt ja | hij zegt nee\n", &r);
        EXPECT (r.chordCharts.size() == 3);
        EXPECT (b.size() == 1 && b[0].lines.size() == 1 && b[0].lines[0].text == "Zij zegt ja | hij zegt nee");

        // "x2" repeats a line; a number in the words doesn't.
        b = parseLyrics ("[V]\nnog een keer x2\nnog eens (3x)\nmax 2 per klant\nx2\n", &r);
        EXPECT (b.size() == 1 && b[0].lines.size() == 2 + 3 + 1 + 1);
        EXPECT (r.linesRepeated == 3);
        if (b.size() == 1 && b[0].lines.size() == 7)
            EXPECT (b[0].lines[0].text == "nog een keer" && b[0].lines[2].text == "nog eens" && b[0].lines[6].text == "x2");

        // Pickups: a short line in parentheses joins the line it leads into.
        b = parseLyrics ("[V]\n(Hij is)\nBest tevreden\n(oeh oeh)\n(aah)\nlaatste regel\n(los)\n", &r);
        EXPECT (b.size() == 1 && r.pickupsJoined == 2);
        EXPECT (b.size() == 1 && b[0].lines.size() == 4);
        if (b.size() == 1 && b[0].lines.size() == 4)
        {
            EXPECT (b[0].lines[0].text == "(Hij is) Best tevreden");
            EXPECT (b[0].lines[1].text == "(oeh oeh)");             // the next line is in parentheses too
            EXPECT (b[0].lines[2].text == "(aah) laatste regel");
            EXPECT (b[0].lines[3].text == "(los)");                 // nothing to lead into
        }

        // A title: one short line set apart before the rest. Not a title: a
        // first line that runs into a header, ends in punctuation, or is alone.
        parseLyrics ("Muggenseizoen\n\n[Verse]\na\n", &r);
        EXPECT (r.title == "Muggenseizoen");
        b = parseLyrics ("Muggenseizoen\n[Verse]\na\n", &r);
        EXPECT (r.title.empty() && b.size() == 2);
        parseLyrics ("Heb je een plas gedaan?\n\n[Verse]\na\n", &r);
        EXPECT (r.title.empty());
        b = parseLyrics ("Alleen deze regel\n", &r);
        EXPECT (r.title.empty() && b.size() == 1);

        // Non-breaking spaces, "()" placeholders.
        b = parseLyrics ("[V]\nIk\xC2\xA0moet de () bonnetjes\xC2\xA0\xC2\xA0" "bewaren\n");
        EXPECT (b.size() == 1 && b[0].lines[0].text == "Ik moet de bonnetjes bewaren");
    }

    std::string readFile (const std::string& path)
    {
        std::ifstream f (path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::size_t lineCount (const std::vector<LyricBlock>& blocks)
    {
        std::size_t n = 0;
        for (const auto& b : blocks)
            n += b.lines.size();
        return n;
    }

    // Real lyrics pasted from the band's Google Doc (tests/fixtures/lyrics).
    void lyricFixtureTests (const std::string& dir)
    {
        LyricImportReport r;

        // Papa die gaat naar de club: headers without brackets, a pickup, a title.
        auto text = readFile (dir + "/papa-die-gaat-naar-de-club.txt");
        EXPECT (! text.empty());
        auto b = parseLyrics (text, &r);
        EXPECT (r.title == "Papa die gaat naar de club");
        EXPECT (r.pickupsJoined == 1 && r.chordCharts.empty());
        EXPECT (b.size() == 6);
        if (b.size() == 6)
        {
            EXPECT (b[0].header == "Verse 1" && b[0].lines.size() == 6);
            EXPECT (b[0].lines[3].text == "(Hij is) Best tevreden samen");
            EXPECT (b[1].header == "Pre" && b[3].header == "Pre" && b[4].header == "Chorus");
            EXPECT (b[5].header == "Verse 3" && b[5].lines.size() == 8);
            EXPECT (b[5].lines[2].text == "De deuren gaan open,");   // NBSPs gone
            EXPECT (b[2].lines[4].text == "Papa heeft plannen,");
        }
        // Linked to a typical parts track.
        Song papa;
        papa.setSections ({ sec ("Intro", 0, 16), sec ("Verse 1", 16, 48), sec ("Pre-Chorus", 48, 64),
                            sec ("Verse 2", 64, 96), sec ("Pre-Chorus", 96, 112), sec ("Chorus", 112, 144),
                            sec ("Verse 3", 144, 176), sec ("Chorus", 176, 208), sec ("Outro", 208, 224) });
        papa.setBlocks (b);
        EXPECT (papa.blockForSection (0) == -1 && papa.blockForSection (8) == -1);
        EXPECT (papa.blockForSection (1) == 0 && papa.blockForSection (2) == 1 && papa.blockForSection (3) == 2);
        EXPECT (papa.blockForSection (4) == 3 && papa.blockForSection (5) == 4 && papa.blockForSection (6) == 5);
        EXPECT (papa.blockForSection (7) == 4);   // the last chorus sings the chorus again

        // Dertig dagen: the chorus written out again without a header, twice.
        b = parseLyrics (readFile (dir + "/dertig-dagen.txt"), &r);
        EXPECT (r.stanzasMatched == 4 && r.title.empty());
        EXPECT (b.size() == 6);
        if (b.size() == 6)
        {
            EXPECT (b[0].header == "Verse" && b[2].header == "Verse" && b[4].header == "Bridge");
            EXPECT (b[1].header == "Chorus" && b[3].header == "Chorus" && b[5].header == "Chorus");
            EXPECT (b[2].lines.size() == 8 && b[3].lines.size() == 8 && b[4].lines.size() == 5);
            EXPECT (b[0].lines[4].text == "Ik moet de bonnetjes bewaren,");
        }
        Song dertig;
        dertig.setSections ({ sec ("Verse 1", 0, 32), sec ("Chorus 1", 32, 64), sec ("Verse 2", 64, 96),
                              sec ("Chorus 2", 96, 128), sec ("Bridge", 128, 144), sec ("Chorus 3", 144, 176) });
        dertig.setBlocks (b);
        for (int i = 0; i < 6; ++i)
            EXPECT (dertig.blockForSection (i) == i);

        // De langste rij: chord charts, Refrein / Brug, repeat markers, "x 2".
        b = parseLyrics (readFile (dir + "/de-langste-rij.txt"), &r);
        EXPECT (r.title == "De langste rij");
        EXPECT (r.chordCharts.size() == 5 && r.repeatMarkers == 2 && r.linesRepeated == 1);
        EXPECT (b.size() == 3);
        if (b.size() == 3)
        {
            EXPECT (b[0].header == "Refrein" && b[0].lines.size() == 16);   // the verses under it: not marked
            EXPECT (b[1].header.empty() && b[1].lines.size() == 12);       // after "Refrein": not linked
            EXPECT (b[2].header == "Brug" && b[2].lines.size() == 5);
            EXPECT (b[2].lines[0].text == "Ik heb bijna niks in mijn mand\xE2\x80\xA6");
            EXPECT (b[2].lines[3].text == b[2].lines[4].text);
            EXPECT (b[0].lines[15].text == "Wij willen, blijven, dromen\xE2\x80\xA6");
        }
        Song rij;
        rij.setSections ({ sec ("Intro", 0, 8), sec ("Chorus", 8, 24), sec ("Verse", 24, 56), sec ("Bridge", 56, 72),
                           sec ("Chorus", 72, 88) });
        rij.setBlocks (b);
        EXPECT (rij.blockForSection (1) == 0 && rij.blockForSection (3) == 2 && rij.blockForSection (4) == 0);
        EXPECT (rij.blockForSection (2) == -1);

        // Parijs and Nedertown: no headers at all. Everything in one block that
        // links nowhere, the chord charts kept aside, the titles skipped.
        b = parseLyrics (readFile (dir + "/parijs.txt"), &r);
        EXPECT (! r.anyHeader && r.title == "Parijs" && r.chordCharts.size() == 4);
        EXPECT (b.size() == 1 && b[0].header.empty() && lineCount (b) == 32);
        b = parseLyrics (readFile (dir + "/nedertown.txt"), &r);
        EXPECT (! r.anyHeader && r.title == "Nedertown" && r.chordCharts.size() == 7);
        EXPECT (lineCount (b) == 14);
        EXPECT (r.chordCharts.size() == 7 && r.chordCharts[3].rfind ("(Roeren:)", 0) == 0);

        // Every fixture survives the text round trip the editor does.
        for (const char* name : { "papa-die-gaat-naar-de-club.txt", "dertig-dagen.txt", "de-langste-rij.txt" })
        {
            const auto first = parseLyrics (readFile (dir + "/" + name));
            const auto again = parseLyrics (lyricsToText (first));
            EXPECT (lyricsToText (again) == lyricsToText (first));
        }
    }
    void chordSceneTests()
    {
        auto song = makeSong();
        auto span = [] (double a, double b, const char* name, double conf = 0.8)
        {
            ChordSpan c;
            c.start = a; c.end = b; c.confidence = conf;
            parseChordName (name, c.chord);
            return c;
        };
        song.setChords ({ span (16, 24, "C"), span (24, 32, "Am/E"), span (32, 40, "F", 0.05), span (44, 48, "G") },
                        Key { 0, false });

        EXPECT (song.chordAt (10) == -1 && song.nextChordAfter (10) == 0 && song.previousChordBefore (10) == -1);
        EXPECT (song.chordAt (16) == 0 && song.chordAt (23.9) == 0 && song.chordAt (24) == 1);
        EXPECT (song.previousChordBefore (25) == 0 && song.nextChordAfter (25) == 2);
        EXPECT (song.chordAt (42) == -1 && song.previousChordBefore (42) == 2 && song.nextChordAfter (42) == 3);
        EXPECT (song.chordAt (50) == -1 && song.nextChordAfter (50) == -1 && song.previousChordBefore (50) == 3);

        DisplaySettings ds;
        ds.showBassNote = false;
        auto sc = buildScene (song, at (26), ds);
        EXPECT (sc.hasChords && sc.chord == "Am/E" && sc.prevChord == "C" && sc.nextChord == "F");
        EXPECT (near (sc.beatsToNextChord, 6) && sc.bassNote.empty() && ! sc.chordUnsure);
        ds.showBassNote = true;
        sc = buildScene (song, at (26), ds);
        EXPECT (sc.chord == "Am" && sc.bassNote == "E");   // the bass on its own, not in the name
        sc = buildScene (song, at (33), ds);
        EXPECT (sc.chord == "F" && sc.chordUnsure);
        sc = buildScene (song, at (42), ds);
        EXPECT (sc.chord.empty() && sc.prevChord == "F" && sc.nextChord == "G");   // a gap
        ds.showChords = false;
        EXPECT (! buildScene (song, at (26), ds).hasChords);
        EXPECT (! buildScene (makeSong(), at (26), DisplaySettings()).hasChords);
    }
    std::string chordAtName (const std::vector<ChordSpan>& chords, double t, const Key& key = Key { 10, true })
    {
        for (const auto& c : chords)
            if (t >= c.start - 1e-9 && t < c.end - 1e-9)
                return chordName (c.chord, key);
        return "-";
    }

    void chartTests()
    {
        const TimeSig sig;
        const std::vector<Section> parts { sec ("intro", 0, 16), sec ("verse", 16, 48), sec ("chorus", 48, 64),
                                           sec ("verse", 64, 96), sec ("chorus", 96, 112) };

        // Notation: halves, "." beats, "-" no chord, empty bar = goes on, "%" = again.
        auto r = chartToChords ("# a comment\n[intro]\n| Bbm | Eb/Bb Bbm | Ab . . Db | - |\n", parts, sig);
        EXPECT (r.unknown.empty() && r.unmatched.empty() && r.partsFilled == 1);
        EXPECT (chordAtName (r.chords, 0.5) == "Bbm" && chordAtName (r.chords, 4.5) == "Eb/Bb" && chordAtName (r.chords, 6.5) == "Bbm");
        EXPECT (chordAtName (r.chords, 8.5) == "Ab" && chordAtName (r.chords, 10.5) == "Ab" && chordAtName (r.chords, 11.5) == "Db");
        EXPECT (chordAtName (r.chords, 12.5) == "-");   // "-": no chord
        r = chartToChords ("[verse]\n| Am | | % | Dm E(7b13) |\n", parts, sig);
        EXPECT (chordAtName (r.chords, 16.5) == "Am" && chordAtName (r.chords, 20.5) == "Am");   // empty bar: goes on
        EXPECT (chordAtName (r.chords, 24.5) == "Am" && chordAtName (r.chords, 28.5) == "Dm" && chordAtName (r.chords, 30.5) == "E7");
        EXPECT (chordAtName (r.chords, 33) == "-");   // a [block] is played once, not looped
        EXPECT (r.chords.size() == 3);   // Am merged across the bars

        // [blocks] in song order: the 2nd [verse] goes to the 2nd verse.
        r = chartToChords ("[verse]\n| C |\n[verse]\n| G |\n[bridge]\n| F |\n", parts, sig);
        EXPECT (chordAtName (r.chords, 17) == "C" && chordAtName (r.chords, 65) == "G");
        EXPECT (r.unmatched.size() == 1 && r.unmatched[0] == "[bridge]");

        // Band chart lines: a progression per kind of part, looped to fill each,
        // with repeats, alternatives and a continuation line.
        r = chartToChords ("Verse/Chorus: ||: Am | | | Dm E(7b13) :||\nIntro: || Fm | Cm |\n   | Bb (Ab) | - ||\n", parts, sig);
        EXPECT (r.unknown.empty() && r.partsFilled == 5);
        EXPECT (chordAtName (r.chords, 1) == "Fm" && chordAtName (r.chords, 5) == "Cm" && chordAtName (r.chords, 9) == "Bb");
        EXPECT (chordAtName (r.chords, 13) == "-");
        EXPECT (chordAtName (r.chords, 17) == "Am" && chordAtName (r.chords, 29) == "Dm" && chordAtName (r.chords, 31) == "E7");
        EXPECT (chordAtName (r.chords, 33) == "Am" && chordAtName (r.chords, 47) == "E7");   // looped through the verse
        EXPECT (chordAtName (r.chords, 49) == "Am" && chordAtName (r.chords, 97) == "Am");   // and the choruses
        // A [block] wins over a label for its part.
        r = chartToChords ("Chorus: | Am |\n[chorus]\n| C |\n", parts, sig);
        EXPECT (chordAtName (r.chords, 49) == "C" && chordAtName (r.chords, 97) == "Am");
        // "x2", unknown tokens reported.
        r = chartToChords ("[verse]\n| C | G | x2\n| Hmm |\n", parts, sig);
        EXPECT (chordAtName (r.chords, 17) == "C" && chordAtName (r.chords, 25) == "C" && chordAtName (r.chords, 29) == "G");
        EXPECT (chordAtName (r.chords, 33) == "G" && chordAtName (r.chords, 37) == "-");   // the unknown bar: G goes on, then the block ends
        EXPECT (r.unknown.size() == 1 && r.unknown[0] == "Hmm");

        // Export -> import gives the same chords (on beat resolution).
        std::vector<ChordSpan> chords;
        auto add = [&chords] (double a, double b, const char* n)
        {
            ChordSpan c; c.start = a; c.end = b; c.confidence = 0.7; parseChordName (n, c.chord); chords.push_back (c);
        };
        add (0, 6, "Bbm"); add (6, 8, "Eb/Bb"); add (8, 11, "Ab"); add (11, 16, "Db");
        add (16, 40, "Am7"); add (44, 48, "E7"); add (48, 64, "F#m7b5"); add (64, 112, "G");
        const auto text = chordsToChart (parts, chords, Key { 10, true }, sig);
        EXPECT (text.find ("[intro]") != std::string::npos && text.find ("Ab . . Db") != std::string::npos);
        r = chartToChords (text, parts, sig);
        EXPECT (r.unknown.empty() && r.unmatched.empty() && r.partsFilled == 5);
        for (double t = 0.5; t < 112; t += 1.0)
            if (chordAtName (r.chords, t) != chordAtName (chords, t))
            {
                std::printf ("chart round trip differs at %.1f: %s vs %s\n", t, chordAtName (r.chords, t).c_str(),
                             chordAtName (chords, t).c_str());
                EXPECT (false);
                break;
            }
        // 6/8 too.
        const TimeSig six { 6, 8 };
        const std::vector<Section> waltz { sec ("verse", 0, 12) };
        std::vector<ChordSpan> c68;
        { ChordSpan c; c.start = 0; c.end = 4.5; parseChordName ("D", c.chord); c68.push_back (c); }
        { ChordSpan c; c.start = 4.5; c.end = 12; parseChordName ("A7", c.chord); c68.push_back (c); }
        r = chartToChords (chordsToChart (waltz, c68, {}, six), waltz, six);
        EXPECT (chordAtName (r.chords, 4.2) == "D" && chordAtName (r.chords, 4.7) == "A7" && chordAtName (r.chords, 11) == "A7");
    }

    void timedLyricsTests()
    {
        auto song = makeSong();   // Intro 0-16, Verse 1 16-48, Chorus 48-80, Verse 2 80-112, Chorus 112-144
        song.setTimeSig (TimeSig());
        EXPECT (near (positionToPpq (13, 2, 3, TimeSig()), 49.5));
        EXPECT (song.stamp (2, 1, 57.5));   // "ch b" in the 1st chorus at 15.3.3
        EXPECT (song.stamp (1, 0, 17.0));   // "v1 a" at 5.2.1

        const auto text = lyricsToTimedText (song);
        EXPECT (text.find ("@15.2.3 ch b") != std::string::npos && text.find ("@5.2.1 v1 a") != std::string::npos);
        EXPECT (text.find ("@") == text.find ("@5.2.1") || text.find ("\"@") != std::string::npos);

        // Into a fresh song: the timings come back.
        Song fresh;
        fresh.setSections (song.sections());
        LyricImportReport rep;
        fresh.setBlocks (parseLyrics (text, &rep));
        EXPECT (rep.timedLines == 2);
        EXPECT (fresh.blocks().size() == song.blocks().size());
        if (fresh.blocks().size() == song.blocks().size())
        {
            EXPECT (near (fresh.blocks()[2].lines[1].offset, 9.5) && near (fresh.blocks()[1].lines[0].offset, 1.0));
            EXPECT (! fresh.blocks()[2].lines[0].timed());
        }
        // A position outside the block's part is ignored; a line without a part keeps it pending.
        auto blocks = parseLyrics ("[Chorus]\n@2.1.1 too early\n");
        Song s2;
        s2.setSections (song.sections());
        s2.setBlocks (blocks);
        EXPECT (! s2.blocks()[0].lines[0].timed() && s2.blocks()[0].lines[0].text == "too early");
        Song s3;
        s3.setBlocks (parseLyrics ("[Chorus]\n@13.1 later\n"));
        EXPECT (! s3.blocks()[0].lines[0].timed());
        s3.setSections (song.sections());
        EXPECT (near (s3.blocks()[0].lines[0].offset, 0.0));
        // Comments are skipped, markdown headings still are headers.
        const auto b = parseLyrics ("# BoothCue lyrics\n## Chorus\nla\n");
        EXPECT (b.size() == 1 && b[0].header == "Chorus" && b[0].lines.size() == 1);

        // A number too long to be a position is no position: the line stays text.
        const auto huge = parseLyrics ("[Chorus]\n@99999999999999999999 la\n@1.99999999999.1 la\n@12.3.4 la\n");
        EXPECT (huge.size() == 1 && huge[0].lines.size() == 3);
        if (huge.size() == 1 && huge[0].lines.size() == 3)
        {
            EXPECT (! huge[0].lines[0].hasAt && huge[0].lines[0].text == "@99999999999999999999 la");
            EXPECT (! huge[0].lines[1].hasAt);
            EXPECT (huge[0].lines[2].hasAt && huge[0].lines[2].atBar == 12 && huge[0].lines[2].text == "la");
        }
    }

    // A file that isn't lyrics dropped on the editor: one endless line. It is
    // cut, the import comes back (std::regex runs out of stack on such a line).
    void longLineTests()
    {
        std::string words, solid (300000, 'a'), bars = "|";
        for (int i = 0; i < 60000; ++i) words += "word ";
        for (int i = 0; i < 4; ++i) bars += " " + solid + " |";
        for (const auto& line : { words, solid, "[" + solid + "]", "(" + solid + ")", "Chorus: " + words, bars, "@1.1.1 " + words + " x2" })
        {
            const auto b = parseLyrics ("[Verse]\nbefore\n" + line + "\nafter\n");
            EXPECT (! b.empty());
            for (const auto& block : b)
            {
                EXPECT (block.header.size() <= 1000);
                for (const auto& l : block.lines)
                    EXPECT (l.text.size() <= 1000);
            }
            EXPECT (! b.empty() && ! b.back().lines.empty() && b.back().lines.back().text == "after");
        }
        // Cut between characters, not inside one.
        std::string accents;
        for (int i = 0; i < 2000; ++i) accents += "\xC3\xA9";
        const auto b = parseLyrics ("[Verse]\nx" + accents + "\n");
        EXPECT (b.size() == 1 && b[0].lines.size() == 1);
        if (b.size() == 1 && b[0].lines.size() == 1)
            EXPECT (b[0].lines[0].text.size() == 999 && b[0].lines[0].text.back() == '\xA9');
    }
}

int main (int argc, char** argv)
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
    gridTests();
    tapCursorTests();
    tapPositionTests();
    importRuleTests();
    chordSceneTests();
    chartTests();
    timedLyricsTests();
    longLineTests();
    if (argc > 1)
        lyricFixtureTests (argv[1]);
    else
        std::printf ("song-check: no lyrics dir given, skipping the lyric fixtures\n");

    if (failures == 0)
        std::printf ("song-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
