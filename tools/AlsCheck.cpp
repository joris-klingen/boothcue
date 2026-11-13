// als-check — the `als-parser` CTest. Runs the real AlsParser over the
// synthetic sets in tests/fixtures (written by tools/make_fixtures.py) and
// fails on any wrong section, a decoy clip that leaks in, or a broken file
// that isn't reported as such.
//
//   als-check <fixtures-dir>          run the checks
//   als-check --dump <set.als> [name] print "start<TAB>end<TAB>name" per section
//                                     (tools/alsmuse_crosscheck.py compares that with ALSmuse)

#include "AlsParser.h"
#include "ChordChart.h"
#include "LyricImport.h"
#include "Song.h"
#include "SetChords.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    bool is (const AlsSection& s, const char* name, double start, double end)
    {
        return s.name == name && std::abs (s.start - start) < 1e-9 && std::abs (s.end - end) < 1e-9;
    }

    void expectSong (const AlsResult& r)
    {
        EXPECT (r.ok);
        EXPECT (r.trackName == "Parts");
        EXPECT (r.trackIndex == 1);
        EXPECT (! r.usedFallback);
        EXPECT (r.sections.size() == 8);
        if (r.sections.size() != 8)
            return;
        // Sorted by time although the file stores them out of order.
        EXPECT (is (r.sections[0], "Intro", 0, 16));
        EXPECT (is (r.sections[1], "Verse 1", 16, 48));
        EXPECT (is (r.sections[2], "Chorus", 48, 80));
        EXPECT (is (r.sections[3], "Verse 2", 80, 112));
        EXPECT (is (r.sections[4], "Chorus", 112, 144));
        EXPECT (is (r.sections[5], "Bridge & \"Solo\"", 144, 162));   // entities decoded, 4.5 bars
        EXPECT (is (r.sections[6], "Chorus", 162, 226));
        EXPECT (is (r.sections[7], "Outro", 234, 250));               // after a 2-bar gap
        // Clip colours (the fixture gives clip i colour i).
        EXPECT (r.sections[0].color == 0 && r.sections[2].color == 2 && r.sections[7].color == 7);
    }

    void fixtureTests (const std::string& dir)
    {
        expectSong (parseAlsFile (dir + "/parts.als"));
        expectSong (parseAlsFile (dir + "/parts.als", "  PARTS "));
        expectSong (parseAlsFile (dir + "/parts-plain.xml"));   // not gzipped

        // The same XML through the in-memory entry point, and fed byte by byte
        // so every tag straddles a chunk boundary.
        std::ifstream in (dir + "/parts-plain.xml", std::ios::binary);
        std::stringstream buffer;
        buffer << in.rdbuf();
        const auto xml = buffer.str();
        EXPECT (! xml.empty());
        expectSong (parseAlsXml (xml));

        {
            struct Counter : XmlScanner::Handler
            {
                int starts = 0, ends = 0, clips = 0;
                void startElement (const std::string& n, const XmlScanner::Attributes&) override
                    { ++starts; if (n == "MidiClip") ++clips; }
                void endElement (const std::string&) override { ++ends; }
            };
            Counter whole, bytewise;
            XmlScanner a (whole), b (bytewise);
            EXPECT (a.feed (xml.data(), xml.size()));
            bool ok = true;
            for (const char c : xml)
                ok = ok && b.feed (&c, 1);
            EXPECT (ok);
            EXPECT (whole.starts > 100 && whole.starts == whole.ends);
            EXPECT (bytewise.starts == whole.starts && bytewise.ends == whole.ends);
            EXPECT (whole.clips == 12);   // every MidiClip in the file: 8 sections + 4 decoys
        }

        // A parts track that is not 2nd, named only through EffectiveName.
        {
            const auto r = parseAlsFile (dir + "/parts-moved.als");
            EXPECT (r.ok && ! r.usedFallback && r.trackIndex == 2);
            EXPECT (r.sections.size() == 2);
            if (r.sections.size() == 2)
            {
                EXPECT (is (r.sections[0], "A", 0, 16));
                EXPECT (is (r.sections[1], "B", 16, 48));
            }
        }

        // No parts track: the 2nd track.
        {
            const auto r = parseAlsFile (dir + "/fallback.als");
            EXPECT (r.ok && r.usedFallback && r.trackIndex == 1 && r.trackName == "Structure");
            EXPECT (r.sections.size() == 2);
            EXPECT (r.trackCount == 3);
        }

        // Another track name on request.
        {
            const auto r = parseAlsFile (dir + "/fallback.als", "keys");
            EXPECT (r.ok && ! r.usedFallback && r.trackIndex == 2);
            EXPECT (r.sections.size() == 1 && r.sections[0].name == "KeysDecoy");
        }

        // Failures are reported, never half a song.
        EXPECT (! parseAlsFile (dir + "/no-parts.als").ok);
        EXPECT (! parseAlsFile (dir + "/no-parts.als").error.empty());
        EXPECT (! parseAlsFile (dir + "/truncated.als").ok);
        EXPECT (parseAlsFile (dir + "/truncated.als").sections.empty());
        EXPECT (! parseAlsFile (dir + "/not-a-set.als").ok);
        EXPECT (! parseAlsFile (dir + "/does-not-exist.als").ok);

        // A save caught at ANY byte: a failure, or the whole structure, never
        // the 2nd track standing in for a parts track that was still to come
        // (parts-moved.als has it 3rd), nor a set with some of its tracks.
        for (const char* name : { "/parts-moved.als", "/notes.als" })
        {
            std::ifstream in (dir + name, std::ios::binary);
            std::stringstream buffer;
            buffer << in.rdbuf();
            const auto whole = buffer.str();
            const auto full = parseAlsFile (dir + name, "parts", true);
            const auto cut = (std::filesystem::temp_directory_path() / "boothcue-als-check-cut.als").string();
            int wrong = 0;
            for (std::size_t n = 1; n < whole.size(); ++n)
            {
                std::ofstream (cut, std::ios::binary).write (whole.data(), static_cast<std::streamsize> (n));
                const auto r = parseAlsFile (cut);
                const auto withNotes = parseAlsFile (cut, "parts", true);
                wrong += r.ok && (r.usedFallback || r.sections.size() != full.sections.size()) ? 1 : 0;
                wrong += withNotes.ok && (withNotes.usedFallback || withNotes.midiTracks.size() != full.midiTracks.size()) ? 1 : 0;
            }
            std::remove (cut.c_str());
            EXPECT (full.ok && whole.size() > 500 && wrong == 0);
        }
    }

    bool hasNote (const AlsMidiTrack& t, double start, int pitch, double length = -1.0)
    {
        for (const auto& n : t.notes)
            if (std::abs (n.start - start) < 1e-9 && n.pitch == pitch && (length < 0.0 || std::abs (n.length - length) < 1e-9))
                return true;
        return false;
    }

    // Notes for chord detection: every clip-timing rule (tools/make_fixtures.py, notes.als).
    void noteTests (const std::string& dir)
    {
        // Without readNotes nothing changes: no tracks, stops at the parts track.
        auto plain = parseAlsFile (dir + "/notes.als");
        EXPECT (plain.ok && plain.midiTracks.empty() && plain.trackCount == 2);

        const auto r = parseAlsFile (dir + "/notes.als", "parts", true);
        EXPECT (r.ok && r.trackName == "Parts" && r.sections.size() == 2);
        EXPECT (r.trackCount == 7);   // read to the end of the track list
        EXPECT (r.midiTracks.size() == 6);   // the audio track isn't one
        if (r.midiTracks.size() != 6)
            return;
        const auto& keys = r.midiTracks[2];
        EXPECT (keys.name == "Keys" && keys.index == 2 && ! keys.muted && ! keys.drums);

        // A: plain clip, the deactivated note left out.
        EXPECT (hasNote (keys, 0, 60, 4) && hasNote (keys, 0, 67, 4) && hasNote (keys, 4, 57, 4));
        EXPECT (! hasNote (keys, 2, 61));
        // B: a 4-beat loop played twice.
        EXPECT (hasNote (keys, 8, 65, 2) && hasNote (keys, 10, 67, 2) && hasNote (keys, 12, 65, 2) && hasNote (keys, 14, 74, 2));
        EXPECT (! hasNote (keys, 16, 65));
        // C: start marker at 2, end marker at 8.
        EXPECT (! hasNote (keys, 14, 40) && ! hasNote (keys, 16, 40));
        EXPECT (hasNote (keys, 16, 41, 4) && hasNote (keys, 20, 42, 2));   // cut at the end marker (content 8)
        bool after = false;
        for (const auto& n : keys.notes)
            after = after || n.pitch == 43;
        EXPECT (! after);
        // D: deactivated clip.
        for (const auto& n : keys.notes)
            EXPECT (n.start < 24.0 - 1e-9 || n.start >= 32.0 - 1e-9);
        // E: loop 2-6, start marker 3, clip 32-40.
        EXPECT (hasNote (keys, 32, 49, 0.5) && hasNote (keys, 34, 50, 0.5));
        EXPECT (hasNote (keys, 35, 48) && hasNote (keys, 36, 49) && hasNote (keys, 38, 50) && hasNote (keys, 39, 48));
        int inE = 0;
        for (const auto& n : keys.notes)
            inE += n.start >= 32.0 - 1e-9 ? 1 : 0;
        EXPECT (inE == 6);
        // Sorted by start.
        for (std::size_t i = 1; i < keys.notes.size(); ++i)
            EXPECT (keys.notes[i - 1].start <= keys.notes[i].start);

        EXPECT (r.midiTracks[3].name == "Bass" && r.midiTracks[3].notes.size() == 6);
        EXPECT (r.midiTracks[4].drums && r.midiTracks[4].notes.size() == 16);
        EXPECT (r.midiTracks[5].muted && r.midiTracks[5].notes.size() == 3);
        EXPECT (r.midiTracks[1].name == "Parts" && r.midiTracks[1].notes.empty());
        EXPECT (r.midiTracks[3].notes[0].velocity == 100);

        // With notes, the structure is the same as without.
        EXPECT (plain.sections.size() == r.sections.size());

        // Chords from the set: Keys + Bass; the drums, the muted track and the
        // parts / length tracks are left out.
        const auto chords = chordsFromSet (r, 4.0);
        EXPECT (chords.anyNotes);
        EXPECT (! chords.fromMuted);
        {
            // Only the muted track has notes (the others are audio by now): it is used after all.
            auto printed = r;
            for (auto& t : printed.midiTracks)
                if (! t.muted)
                    t.notes.clear();
            const auto fromMuted = chordsFromSet (printed, 4.0);
            EXPECT (fromMuted.anyNotes && fromMuted.fromMuted && ! fromMuted.result.spans.empty());
            EXPECT (chordName (fromMuted.result.spans.front().chord, fromMuted.result.key) == "Db");
        }
        EXPECT (chords.chordTracks == std::vector<std::string> { "Keys" });
        EXPECT (chords.bassTracks == std::vector<std::string> { "Bass" });
        auto at = [&chords] (double t)
        {
            for (const auto& sp : chords.result.spans)
                if (t >= sp.start && t < sp.end)
                    return chordName (sp.chord, chords.result.key);
            return std::string ("-");
        };
        EXPECT (at (1) == "C" && at (5) == "Am" && at (9) == "F" && at (11) == "G" && at (13) == "F" && at (15) == "G");
        if (at (1) != "C" || at (5) != "Am" || at (9) != "F" || at (11) != "G")
            std::printf ("chords: %s %s %s %s\n", at (1).c_str(), at (5).c_str(), at (9).c_str(), at (11).c_str());
        EXPECT (! chordsFromSet (plain).anyNotes);   // read without notes: nothing to detect
    }

    void xmlTests()
    {
        const char* head = "<Ableton><LiveSet><Tracks><MidiTrack><Name><UserName Value='x'/></Name></MidiTrack>"
                           "<MidiTrack><Name><UserName Value=\"parts\"/></Name><DeviceChain><MainSequencer>"
                           "<ClipTimeable><ArrangerAutomation><Events>";
        const char* tail = "</Events></ArrangerAutomation></ClipTimeable></MainSequencer></DeviceChain></MidiTrack>"
                           "</Tracks></LiveSet></Ableton>";
        auto parse = [&] (const std::string& clips) { return parseAlsXml (head + clips + tail); };

        // Numbers: decimals, exponents, and no dependence on the C locale.
        auto r = parse ("<MidiClip Time=\"1.5\"><CurrentEnd Value=\"1.25e1\"/><Name Value=\"A &#x41;&#233;\"/></MidiClip>");
        EXPECT (r.ok && r.sections.size() == 1);
        if (r.sections.size() == 1)
        {
            EXPECT (std::abs (r.sections[0].start - 1.5) < 1e-12);
            EXPECT (std::abs (r.sections[0].end - 12.5) < 1e-12);
            EXPECT (r.sections[0].name == "A A\xC3\xA9");
        }

        // Clips without a usable start or end are skipped; '>' inside a value is fine.
        r = parse ("<MidiClip Time=\"x\"><CurrentEnd Value=\"4\"/></MidiClip>"
                   "<MidiClip Time=\"0\"></MidiClip>"
                   "<MidiClip Time=\"8\"><CurrentEnd Value=\"8\"/></MidiClip>"
                   "<MidiClip Time=\"0\"><CurrentEnd Value=\"4\"/><Name Value=\"a>b\"/></MidiClip>");
        EXPECT (r.ok && r.sections.size() == 1);
        EXPECT (r.sections.size() == 1 && r.sections[0].name == "a>b");

        // Comments, CDATA and text are skipped.
        r = parse ("<!-- <MidiClip Time=\"0\"> --><![CDATA[ <MidiClip> ]]> text "
                   "<MidiClip Time=\"0\"><CurrentEnd Value=\"4\"/><Name Value=\"ok\"/></MidiClip>");
        EXPECT (r.ok && r.sections.size() == 1);

        // An empty parts track is still a valid (empty) structure.
        r = parse ("");
        EXPECT (r.ok && r.sections.empty() && r.trackName == "parts");

        EXPECT (! parseAlsXml ("<Ableton><LiveSet><Tracks><MidiTrack =></Tracks>").ok);
        EXPECT (! parseAlsXml ("").ok);

        // A track list that is cut off (a save caught halfway, a damaged file)
        // is a failure, never the 2nd track standing in for a parts track that
        // was still to come, nor half the tracks for chord detection.
        const std::string twoTracks = "<Ableton><LiveSet><Tracks><MidiTrack><Name><UserName Value='x'/></Name></MidiTrack>"
                                      "<MidiTrack><Name><UserName Value='y'/></Name></MidiTrack>";
        const std::string partsTrack = "<MidiTrack><Name><UserName Value='parts'/></Name></MidiTrack>";
        r = parseAlsXml (twoTracks + "<MidiTrack><Name><UserName Value='parts'/></Name>");
        EXPECT (! r.ok && ! r.error.empty() && r.sections.empty());
        EXPECT (parseAlsXml (twoTracks + "</Tracks>").ok);                 // complete: the fallback
        EXPECT (parseAlsXml (twoTracks + partsTrack).ok);                  // the parts track is all that's needed...
        r = parseAlsXml (twoTracks + partsTrack + "<MidiTrack>", "parts", true);
        EXPECT (! r.ok && r.midiTracks.empty());                           // ...but not when the notes are wanted
        EXPECT (parseAlsXml (twoTracks + partsTrack + "</Tracks>", "parts", true).midiTracks.size() == 3);

        // Numbers no set has: skipped, not carried along as inf or wrapped ints.
        r = parse ("<MidiClip Time=\"0\"><CurrentEnd Value=\"1e99999999999\"/></MidiClip>"
                   "<MidiClip Time=\"0\"><CurrentEnd Value=\"1e300\"/></MidiClip>"
                   "<MidiClip Time=\"-1e300\"><CurrentEnd Value=\"4\"/></MidiClip>"
                   "<MidiClip Time=\"4\"><CurrentEnd Value=\"8\"/><Color Value=\"1e300\"/></MidiClip>");
        EXPECT (r.ok && r.sections.size() == 1);
        EXPECT (r.sections.size() == 1 && r.sections[0].start == 4.0 && r.sections[0].color == -1);

        // Character references that are no character stay as written.
        r = parse ("<MidiClip Time=\"0\"><CurrentEnd Value=\"4\"/><Name Value=\"a&#0;b&#x110000;c&#xD800;d&#xZ;e&#65;\"/></MidiClip>");
        EXPECT (r.sections.size() == 1 && r.sections[0].name == "a&#0;b&#x110000;c&#xD800;d&#xZ;eA");

        // Notes: a key or velocity out of MIDI's range, and a loop of next to
        // nothing under a long clip (unrolled, it would be millions of notes).
        auto notes = [&] (const std::string& loop, const std::string& keyTracks)
        {
            const auto set = parseAlsXml (head + ("<MidiClip Time=\"0\"><CurrentEnd Value=\"100000\"/><Loop>" + loop
                                                  + "</Loop><Notes><KeyTracks>" + keyTracks + "</KeyTracks></Notes></MidiClip>")
                                              + tail, "parts", true);
            return set.ok && set.midiTracks.size() == 2 ? set.midiTracks[1].notes : std::vector<AlsNote>();
        };
        const std::string event = "<Notes><MidiNoteEvent Time=\"0\" Duration=\"1\" Velocity=\"1e300\"/></Notes>";
        auto n = notes ("<LoopStart Value=\"0\"/><LoopEnd Value=\"4\"/>",
                        "<KeyTrack>" + event + "<MidiKey Value=\"60\"/></KeyTrack>"
                        "<KeyTrack>" + event + "<MidiKey Value=\"1e300\"/></KeyTrack>"
                        "<KeyTrack>" + event + "<MidiKey Value=\"128\"/></KeyTrack>");
        EXPECT (n.size() == 1 && n[0].pitch == 60 && n[0].velocity == 127);
        n = notes ("<LoopStart Value=\"0\"/><LoopEnd Value=\"0.0001\"/><LoopOn Value=\"true\"/>",
                   "<KeyTrack><Notes>" + [] { std::string twenty; for (int i = 0; i < 20; ++i)
                                                  twenty += "<MidiNoteEvent Time=\"0\" Duration=\"1\" Velocity=\"100\"/>";
                                              return twenty; }()
                   + "</Notes><MidiKey Value=\"60\"/></KeyTrack>");
        EXPECT (! n.empty() && n.size() <= 1000000);
    }
}

int main (int argc, char** argv)
{
    if (argc >= 3 && std::strcmp (argv[1], "--dump") == 0)
    {
        const auto r = parseAlsFile (argv[2], argc >= 4 ? argv[3] : "parts");
        if (! r.ok)
        {
            std::fprintf (stderr, "error: %s\n", r.error.c_str());
            return 1;
        }
        std::fprintf (stderr, "track %d \"%s\"%s\n", r.trackIndex + 1, r.trackName.c_str(),
                      r.usedFallback ? " (fallback: 2nd track)" : "");
        for (const auto& s : r.sections)
            std::printf ("%.6f\t%.6f\t%s\n", s.start, s.end, s.name.c_str());
        return 0;
    }

    if (argc >= 3 && std::strcmp (argv[1], "--chart") == 0)
    {
        // A set's detected chords as a BoothCue chart (the Chords tab's format).
        const auto r = parseAlsFile (argv[2], "parts", true);
        if (! r.ok)
        {
            std::fprintf (stderr, "error: %s\n", r.error.c_str());
            return 1;
        }
        const auto c = chordsFromSet (r, 4.0);
        std::vector<Section> sections;
        for (const auto& sec : r.sections)
            sections.push_back ({ sec.name, sec.start, sec.end, sec.color });
        std::printf ("%s", chordsToChart (sections, c.result.spans, c.result.key, TimeSig()).c_str());
        return 0;
    }

    if (argc >= 3 && std::strcmp (argv[1], "--chords") == 0)
    {
        // A real set: its MIDI tracks, what the detector made of them, and the
        // chords bar by bar under the sections (for tuning).
        const auto r = parseAlsFile (argv[2], "parts", true);
        if (! r.ok)
        {
            std::fprintf (stderr, "error: %s\n", r.error.c_str());
            return 1;
        }
        // Optional: a bar length in beats, and / or a lyrics file to link.
        double bar = 4.0;
        std::string lyricsPath;
        for (int a = 3; a < argc; ++a)
        {
            if (std::atof (argv[a]) > 0.0)
                bar = std::atof (argv[a]);
            else
                lyricsPath = argv[a];
        }
        const auto c = chordsFromSet (r, bar);
        Song song;
        std::vector<Section> sections;
        for (const auto& sec : r.sections)
            sections.push_back ({ sec.name, sec.start, sec.end, sec.color });
        song.setSections (sections);
        if (! lyricsPath.empty())
        {
            std::ifstream f (lyricsPath, std::ios::binary);
            std::stringstream text;
            text << f.rdbuf();
            song.setBlocks (parseLyrics (text.str()));
        }
        static const char* roleNames[] = { "auto", "chords", "bass", "melody", "ignore" };
        std::printf ("MIDI tracks:\n");
        for (std::size_t i = 0; i < r.midiTracks.size(); ++i)
        {
            const auto& t = r.midiTracks[i];
            const auto role = i < c.result.roles.size() ? c.result.roles[i] : TrackRole::ignore;
            std::printf ("  %2d %-24s %5zu notes%s%s  -> %s\n", t.index + 1, t.name.c_str(), t.notes.size(),
                         t.muted ? " muted" : "", t.drums ? " drums" : "", roleNames[static_cast<int> (role)]);
        }
        std::printf ("key: %s, %zu chords\n", keyName (c.result.key).c_str(), c.result.spans.size());
        std::size_t k = 0;
        for (const auto& sec : r.sections)
        {
            std::printf ("\n[%s] %.0f-%.0f", sec.name.empty() ? "(no name)" : sec.name.c_str(), sec.start, sec.end);
            if (! lyricsPath.empty())
            {
                const int si = static_cast<int> (&sec - &r.sections[0]);
                const int b = song.blockForSection (si);
                if (b < 0)
                    std::printf ("   lyrics: -");
                else
                    std::printf ("   lyrics: [%s] \"%s\"...", song.blocks()[static_cast<std::size_t> (b)].header.c_str(),
                                 song.blocks()[static_cast<std::size_t> (b)].lines.empty() ? ""
                                     : song.blocks()[static_cast<std::size_t> (b)].lines[0].text.c_str());
            }
            std::printf ("\n ");
            for (double t = std::floor (sec.start / bar) * bar; t < sec.end - 1e-6; t += bar)
            {
                std::string cell;
                for (k = 0; k < c.result.spans.size(); ++k)
                {
                    const auto& sp = c.result.spans[k];
                    if (sp.end > t + 1e-6 && sp.start < t + bar - 1e-6)
                        cell += (cell.empty() ? "" : " ") + chordName (sp.chord, c.result.key)
                                + (sp.confidence < 0.15 ? "?" : "");
                }
                std::printf ("| %-12s", cell.empty() ? "-" : cell.c_str());
            }
            std::printf ("|\n");
        }
        return 0;
    }

    if (argc < 2)
    {
        std::printf ("usage: als-check <fixtures-dir> | --dump <set.als> [track name]"
                     " | --chords <set.als> [bar beats] [lyrics.txt] | --chart <set.als>\n");
        return 2;
    }

    fixtureTests (argv[1]);
    noteTests (argv[1]);
    xmlTests();

    if (failures == 0)
        std::printf ("als-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
