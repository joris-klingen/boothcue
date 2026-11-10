// chord-check — the `chord-detect` CTest. Plays the Bovenburen chord charts
// (Muggenseizoen, Parijs, Nedertown, Garnalen) as synthetic MIDI in the ways
// keys players hide the harmony (arpeggios, passing and grace notes,
// suspensions, a melody on top, a walking bass, inversions) and checks, beat by
// beat, that detectChords names the chord the chart says.
//
//   chord-check            run the checks
//   chord-check --explain  also print every case beat by beat

#include "Chords.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace boothcue;

namespace
{
    int failures = 0;
    bool explain = false;

    void expect (bool ok, const char* what, int line)
    {
        if (! ok)
        {
            std::printf ("FAIL (line %d): %s\n", line, what);
            ++failures;
        }
    }
    #define EXPECT(cond) expect ((cond), #cond, __LINE__)

    Chord chord (const char* name)
    {
        Chord c;
        const bool ok = parseChordName (name, c);
        if (! ok)
            std::printf ("FAIL: test chord \"%s\" doesn't parse\n", name);
        return c;
    }

    // A chart: one entry per bar, chords split the bar evenly ("Dm E7").
    struct Bar { std::vector<const char*> chords; };

    struct Case
    {
        const char* name;
        ChordInput input;
        std::vector<Chord> expected;   // per beat; no chord = not counted
        double songEnd = 0.0;
    };

    // Close voicing around middle C, root position (the chord's own bass, if
    // it is a slash chord, is left to the bass track or a low note).
    std::vector<int> voicing (const Chord& c, int around = 60)
    {
        std::vector<int> out;
        int root = c.root;
        while (root + 60 < around - 6) root += 12;
        while (root + 60 > around + 5) root -= 12;
        for (const int i : chordIntervals (c.quality))
            out.push_back (60 + root + i);
        return out;
    }

    int low (int pitchClass, int base = 36)   // a bass note at or above `base`
    {
        int p = base;
        while (((p % 12) + 12) % 12 != pitchClass) ++p;
        return p;
    }

    void note (ChordInput& in, int track, double start, double length, int pitch, int velocity = 90)
    {
        in.notes.push_back ({ start, length, pitch, velocity, track });
    }

    // Lays the chart out: calls `play` for every chord with its start and length.
    template <typename Play>
    std::vector<Chord> layOut (const std::vector<Bar>& chart, Play&& play, double beatsPerBar = 4.0)
    {
        std::vector<Chord> perBeat;
        double t = 0.0;
        for (const auto& bar : chart)
        {
            const double each = beatsPerBar / static_cast<double> (bar.chords.size());
            for (const auto* name : bar.chords)
            {
                const auto c = chord (name);
                play (c, t, each);
                for (int b = 0; b < static_cast<int> (std::round (each)); ++b)
                    perBeat.push_back (c);
                t += each;
            }
        }
        return perBeat;
    }

    // ---- the cases -------------------------------------------------------------------------

    std::vector<Bar> muggenseizoen()
    {
        return { { { "Am" } }, { { "Am" } }, { { "Am" } }, { { "Dm", "E(7b13)" } },
                 { { "Am" } }, { { "Am" } }, { { "Am" } }, { { "Dm", "E(7b13)" } } };
    }
    std::vector<Bar> parijsVerse()
    {
        return { { { "Fm" } }, { { "Fmsus2" } }, { { "Cm" } }, { { "Bb" } },
                 { { "Ab" } }, { { "Cm" } }, { { "Db" } }, { { "Bb" } } };
    }
    std::vector<Bar> parijsChorus()
    {
        return { { { "Ab" } }, { { "Cm" } }, { { "Ab", "Bb" } }, { { "Cm" } },
                 { { "F" } }, { { "Ab" } }, { { "Bb" } }, { { "Bb" } } };
    }
    std::vector<Bar> nedertownChorus()
    {
        return { { { "Dm7" } }, { { "C/G" } }, { { "Cm7" } }, { { "Bb/F", "C/G" } },
                 { { "Dm7" } }, { { "C/G" } }, { { "Cm7" } }, { { "Bb/F", "C/G" } },
                 { { "Db/Ab", "Eb" } }, { { "Db/Ab", "Eb" } } };
    }
    std::vector<Bar> garnalenVerse()
    {
        return { { { "C" } }, { { "G" } }, { { "C6" } }, { { "G" } },
                 { { "Fmaj7" } }, { { "Em" } }, { { "Dm", "G" } }, { { "C" } } };
    }

    // 1. Block chords with the root in a bass track: the baseline.
    Case blockChords()
    {
        Case k { "Muggenseizoen: block chords + bass", {}, {} };
        k.input.tracks = { { "keys" }, { "bass" } };
        k.expected = layOut (muggenseizoen(), [&] (const Chord& c, double t, double len)
        {
            for (const int p : voicing (c))
                note (k.input, 0, t, len, p);
            note (k.input, 1, t, len, low (c.bass));
        });
        k.songEnd = 32;
        return k;
    }

    // 2. Arpeggiated eighths, no bass track: the lowest note is the bass.
    Case arpeggios()
    {
        Case k { "Parijs verse: arpeggiated 8ths, no bass track", {}, {} };
        k.input.tracks = { { "piano" } };
        k.expected = layOut (parijsVerse(), [&] (const Chord& c, double t, double len)
        {
            auto v = voicing (c, 62);
            v.insert (v.begin(), low (c.root, 41));          // left hand root
            for (int i = 0; i < static_cast<int> (len * 2); ++i)
                note (k.input, 0, t + i * 0.5, 0.5, v[static_cast<std::size_t> (i) % v.size()]);
        });
        k.songEnd = 32;
        return k;
    }

    // 3. Passing notes and grace notes in the top voice, and a walking bass.
    Case embellished()
    {
        Case k { "Parijs chorus: passing + grace notes, walking bass", {}, {} };
        k.input.tracks = { { "keys" }, { "bass" } };
        k.expected = layOut (parijsChorus(), [&] (const Chord& c, double t, double len)
        {
            const auto v = voicing (c);
            for (std::size_t i = 0; i + 1 < v.size(); ++i)
                note (k.input, 0, t, len, v[i]);
            // Top voice: chord tone, a passing note a step up off the beat, the
            // next step on, and a grace note before each beat.
            const int top = v.back();
            for (double b = 0; b < len; b += 1.0)
            {
                note (k.input, 0, t + b - 0.08, 0.08, top - 1, 70);          // grace
                note (k.input, 0, t + b, 0.5, top);
                note (k.input, 0, t + b + 0.5, 0.25, top + 2, 80);            // passing up...
                note (k.input, 0, t + b + 0.75, 0.25, top + 4 > top + 2 ? top + 1 : top, 80);   // ...and a neighbour back
            }
            // Walking bass: root on the beat, approach notes in between.
            const int r = low (c.root);
            for (double b = 0; b < len; b += 1.0)
            {
                note (k.input, 1, t + b, 0.5, b == 0 ? r : r + (b == 2 ? 7 : 0));
                note (k.input, 1, t + b + 0.5, 0.5, r + 1, 80);                // chromatic passing
            }
        });
        k.songEnd = 32;
        return k;
    }

    // 4. Inversions and slash chords from the bass track.
    Case inversions()
    {
        Case k { "Nedertown chorus: slash chords, Db/Ab, Bb/F", {}, {} };
        k.input.tracks = { { "rhodes" }, { "bass" } };
        k.expected = layOut (nedertownChorus(), [&] (const Chord& c, double t, double len)
        {
            for (const int p : voicing (c, 64))
                note (k.input, 0, t, len, p);
            note (k.input, 1, t, len, low (c.bass, 38));
        });
        k.songEnd = 40;
        return k;
    }

    // 5. A melody on top, full of non-chord tones, on its own track.
    Case melodyOnTop()
    {
        Case k { "Garnalen verse: melody track on top", {}, {} };
        k.input.tracks = { { "pad" }, { "lead" }, { "bass" } };
        int step = 0;
        k.expected = layOut (garnalenVerse(), [&] (const Chord& c, double t, double len)
        {
            for (const int p : voicing (c, 58))
                note (k.input, 0, t, len, p, 70);
            note (k.input, 2, t, len, low (c.root));
            // A stepwise tune in the C major scale, on and off the beat.
            static const int scale[] = { 72, 74, 76, 77, 79, 77, 76, 74 };
            for (double b = 0; b < len; b += 0.5)
                note (k.input, 1, t + b, 0.5, scale[step++ % 8], 100);
        });
        k.songEnd = 32;
        return k;
    }

    // 6. Suspensions: 4-3 resolving in the bar (-> the plain chord), and a sus
    //    chord that stays (-> sus4).
    Case suspensions()
    {
        Case k { "Suspensions: Dsus4-D resolving, Dsus4 held", {}, {} };
        k.input.tracks = { { "keys" }, { "bass" } };
        auto bar = [&] (double t, bool resolve)
        {
            note (k.input, 0, t, 4, 62);                      // D
            note (k.input, 0, t, 4, 69);                      // A
            note (k.input, 0, t, resolve ? 2 : 4, 67);        // G (the 4th)...
            if (resolve)
                note (k.input, 0, t + 2, 2, 66);              // ...down to F# (the 3rd)
            note (k.input, 1, t, 4, 38);
        };
        bar (0, true);
        bar (4, true);
        bar (8, false);
        for (const int p : { 55, 59, 62 })                    // then G
            note (k.input, 0, 12, 4, p);
        note (k.input, 1, 12, 4, 43);
        for (int b = 0; b < 8; ++b) k.expected.push_back (chord ("D"));
        for (int b = 0; b < 4; ++b) k.expected.push_back (chord ("Dsus4"));
        for (int b = 0; b < 4; ++b) k.expected.push_back (chord ("G"));
        k.songEnd = 16;
        return k;
    }

    // ---- running them ----------------------------------------------------------------------

    Chord chordAt (const ChordResult& r, double t)
    {
        for (const auto& s : r.spans)
            if (t >= s.start - 1e-6 && t < s.end - 1e-6)
                return s.chord;
        return Chord();
    }

    // Fraction of beats with the right root and quality (and bass, if asked).
    double accuracy (const Case& k, const ChordResult& r, bool withBass)
    {
        int right = 0, counted = 0;
        for (std::size_t b = 0; b < k.expected.size(); ++b)
        {
            const auto& want = k.expected[b];
            if (! want.isChord())
                continue;
            ++counted;
            const auto got = chordAt (r, static_cast<double> (b) + 0.5);
            const bool ok = got.root == want.root && got.quality == want.quality && (! withBass || got.bass == want.bass);
            right += ok ? 1 : 0;
            if (explain)
            {
                std::printf ("    beat %3zu  want %-8s got %-8s %s", b + 1, chordName (want, r.key).c_str(),
                             chordName (got, r.key).c_str(), ok ? "" : "<--");
                if (! ok && b < r.slices.size())
                {
                    const auto& s = r.slices[b];
                    static const char* names[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
                    std::printf ("   bass %s |", s.bass >= 0 ? names[s.bass] : "-");
                    for (int p = 0; p < 12; ++p)
                        if (s.weight[p] > 0.01)
                            std::printf (" %s:%.2f", names[p], s.weight[p]);
                    std::printf (" |");
                    for (const auto& c : s.top)
                        std::printf (" %s %.2f", chordName (c.chord, r.key).c_str(), c.score);
                }
                std::printf ("\n");
            }
        }
        return counted > 0 ? static_cast<double> (right) / counted : 1.0;
    }

    // Played by a person: every note a little early or late, and of uneven
    // length and loudness (deterministic, so the check is repeatable).
    Case humanised (Case k)
    {
        unsigned seed = 12345;
        auto rnd = [&seed] { seed = seed * 1103515245u + 12345u; return static_cast<double> ((seed >> 16) & 0x7fff) / 32767.0; };
        for (auto& n : k.input.notes)
        {
            const double shift = (rnd() - 0.5) * 0.08;    // up to +-0.04 beat
            n.start = std::max (0.0, n.start + shift);
            n.length = std::max (0.05, n.length * (0.85 + 0.2 * rnd()) - shift * 0.5);
            n.velocity = 60 + static_cast<int> (rnd() * 60);
        }
        k.name = "  ...humanised (+-0.04 beat, uneven)";
        return k;
    }

    double run (const Case& k, bool withBass = false, bool filter = true)
    {
        ChordSettings settings;
        settings.explain = explain;
        settings.filterEmbellishments = filter;
        const auto r = detectChords (k.input, k.songEnd, settings);
        if (explain)
            std::printf ("  %s  (key %s)\n", k.name, keyName (r.key).c_str());
        const double a = accuracy (k, r, withBass);
        std::printf ("  %-55s %5.1f%%%s%s\n", k.name, 100.0 * a, withBass ? " (with bass)" : "",
                     filter ? "" : " (filtering OFF)");
        return a;
    }

    void nameTests()
    {
        // Every chord in the Bovenburen charts parses.
        for (const char* n : { "Am", "Dm", "E(7b13)", "F", "Am/E", "E", "Esus4", "Fm", "Fmsus2", "Cm", "Bb", "Ab",
                               "Db", "Dm7", "C/G", "Csus4", "Cm7", "Bb/F", "Db/Ab", "Eb", "Gm", "G", "C", "C6",
                               "Fmaj7", "Em" })
        {
            Chord c;
            EXPECT (parseChordName (n, c) && c.isChord());
        }
        Chord c;
        EXPECT (parseChordName ("-", c) && ! c.isChord());
        EXPECT (! parseChordName ("Verse", c) && ! parseChordName ("hallo", c));
        EXPECT (parseChordName ("E(7b13)", c) && c.root == 4 && c.quality == Quality::dom7);
        EXPECT (parseChordName ("Fmsus2", c) && c.root == 5 && c.quality == Quality::minAdd9);
        EXPECT (parseChordName ("Db/Ab", c) && c.root == 1 && c.bass == 8 && c.quality == Quality::major);
        EXPECT (parseChordName ("F#m7b5", c) && c.root == 6 && c.quality == Quality::halfDim7);

        // Spelling follows the key.
        const Key fMajor { 5, false }, eMajor { 4, false }, cMajor { 0, false }, fMinor { 5, true };
        EXPECT (chordName (chord ("Bb"), fMajor) == "Bb");
        EXPECT (chordName (chord ("A#m"), fMinor) == "Bbm");
        EXPECT (chordName (chord ("Gbm"), eMajor) == "F#m");
        EXPECT (chordName (chord ("Bb/F"), cMajor) == "Bb/F");
        EXPECT (chordName (chord ("F#"), cMajor) == "F#");
        EXPECT (chordName (chord ("Db/Ab"), fMinor) == "Db/Ab");
        EXPECT (chordName (chord ("Dm7"), cMajor) == "Dm7");
        EXPECT (chordName (Chord(), cMajor) == "N.C.");
        EXPECT (keyName (fMinor) == "F minor" && keyName (cMajor) == "C major");
    }

    void silenceTests()
    {
        // Two empty bars, then chords: nothing is invented in the silence; a
        // rest inside the song keeps the chord.
        ChordInput in;
        in.tracks = { { "keys" } };
        for (const int p : { 57, 60, 64 }) in.notes.push_back ({ 8, 4, p, 90, 0 });     // Am, bar 3
        for (const int p : { 57, 62, 65 }) in.notes.push_back ({ 16, 4, p, 90, 0 });    // Dm, bar 5 (bar 4 empty)
        const auto r = detectChords (in, 20);
        EXPECT (! r.spans.empty() && std::abs (r.spans.front().start - 8.0) < 1e-9);
        EXPECT (! chordAt (r, 2).isChord());
        const auto rest = chordName (chordAt (r, 13.5), r.key);
        if (rest != "Am")
        {
            std::printf ("silence test: got %s at 13.5; spans:", rest.c_str());
            for (const auto& sp : r.spans)
                std::printf (" [%.1f-%.1f %s]", sp.start, sp.end, chordName (sp.chord, r.key).c_str());
            std::printf ("\n");
        }
        EXPECT (rest == "Am");   // the empty bar keeps Am
        const auto dm = chordName (chordAt (r, 17.5), r.key);
        if (dm != "Dm/A")
            std::printf ("silence test: got %s at 17.5\n", dm.c_str());
        EXPECT (dm == "Dm/A");   // A is its lowest note
        EXPECT (detectChords ({}, 16).spans.empty());
    }
}

int main (int argc, char** argv)
{
    explain = argc > 1 && std::strcmp (argv[1], "--explain") == 0;

    nameTests();
    silenceTests();

    std::printf ("chord-check: accuracy per beat\n");
    const auto block = blockChords();
    EXPECT (run (block) >= 0.99);
    EXPECT (run (block, true) >= 0.99);
    EXPECT (run (arpeggios()) >= 0.90);
    EXPECT (run (embellished()) >= 0.90);
    const auto inv = inversions();
    EXPECT (run (inv) >= 0.90);
    EXPECT (run (inv, true) >= 0.90);
    EXPECT (run (melodyOnTop()) >= 0.90);
    EXPECT (run (suspensions()) >= 0.99);

    // Real playing is never on the grid.
    EXPECT (run (humanised (blockChords())) >= 0.95);
    EXPECT (run (humanised (arpeggios())) >= 0.85);
    EXPECT (run (humanised (embellished())) >= 0.90);
    EXPECT (run (humanised (inversions()), true) >= 0.90);
    EXPECT (run (humanised (melodyOnTop())) >= 0.90);
    EXPECT (run (humanised (suspensions())) >= 0.90);

    // The filtering is what makes the embellished parts work.
    const double embellishedOff = run (embellished(), false, false);
    const double suspensionsOff = run (suspensions(), false, false);
    EXPECT (embellishedOff < 0.9 || suspensionsOff < 0.9);

    // The key of each song.
    EXPECT (keyName (detectChords (melodyOnTop().input, 32).key) == "C major");
    const auto parijsKey = detectChords (arpeggios().input, 32).key;
    EXPECT (keyName (parijsKey) == "F minor" || keyName (parijsKey) == "Ab major");

    // Roles decided from the notes.
    const auto roles = detectChords (melodyOnTop().input, 32).roles;
    EXPECT (roles.size() == 3 && roles[0] == TrackRole::chords && roles[1] == TrackRole::melody && roles[2] == TrackRole::bass);

    if (failures == 0)
        std::printf ("chord-check: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
