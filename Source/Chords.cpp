#include "Chords.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <limits>

namespace boothcue
{

namespace
{
    constexpr double kEps = 1.0e-6;
    constexpr double kTouch = 0.06;   // beats: notes this close count as joined

    struct QualityInfo
    {
        Quality quality;
        std::vector<int> intervals;   // semitones above the root
        const char* suffix;
        double complexity;            // a small handicap for richer chords
    };

    const std::vector<QualityInfo>& qualities()
    {
        static const std::vector<QualityInfo> q = {
            { Quality::major,    { 0, 4, 7 },      "",     0.00 },
            { Quality::minor,    { 0, 3, 7 },      "m",    0.00 },
            { Quality::dim,      { 0, 3, 6 },      "dim",  0.05 },
            { Quality::aug,      { 0, 4, 8 },      "aug",  0.06 },
            { Quality::sus2,     { 0, 2, 7 },      "sus2", 0.04 },
            { Quality::sus4,     { 0, 5, 7 },      "sus4", 0.03 },
            { Quality::dom7,     { 0, 4, 7, 10 },  "7",    0.03 },
            { Quality::maj7,     { 0, 4, 7, 11 },  "maj7", 0.04 },
            { Quality::min7,     { 0, 3, 7, 10 },  "m7",   0.03 },
            { Quality::halfDim7, { 0, 3, 6, 10 },  "m7b5", 0.05 },
            { Quality::dim7,     { 0, 3, 6, 9 },   "dim7", 0.06 },
            { Quality::six,      { 0, 4, 7, 9 },   "6",    0.05 },
            { Quality::min6,     { 0, 3, 7, 9 },   "m6",   0.05 },
            { Quality::add9,     { 0, 2, 4, 7 },   "add9", 0.05 },
            { Quality::minAdd9,  { 0, 2, 3, 7 },   "m(add9)", 0.05 },
            { Quality::power,    { 0, 7 },         "5",    0.20 },
        };
        return q;
    }

    const QualityInfo& infoFor (Quality q)
    {
        for (const auto& i : qualities())
            if (i.quality == q)
                return i;
        return qualities().front();
    }

    // How much each chord tone matters when it is (or isn't) there.
    double importance (Quality q, int interval)
    {
        if (interval == 0)
            return 1.0;
        if (interval == 3 || interval == 4)
            return 1.0;                                       // the third
        if (interval == 7)
            return 0.5;                                       // the plain fifth: often left out
        if ((q == Quality::sus2 && interval == 2) || (q == Quality::sus4 && interval == 5))
            return 1.0;
        return 0.85;                                          // sevenths, sixths, altered fifths, the ninth
    }

    int pc (int pitch) { return ((pitch % 12) + 12) % 12; }

    bool onBeat (double t)
    {
        return std::abs (t - std::round (t)) < kTouch;
    }

    double overlap (double a0, double a1, double b0, double b1)
    {
        return std::max (0.0, std::min (a1, b1) - std::max (a0, b0));
    }

    // ---- 1. track roles -------------------------------------------------------------

    std::vector<TrackRole> decideRoles (const ChordInput& in)
    {
        std::vector<TrackRole> roles;
        for (std::size_t t = 0; t < in.tracks.size(); ++t)
        {
            auto role = in.tracks[t].role;
            if (role == TrackRole::automatic)
            {
                std::vector<const ChordNote*> notes;
                for (const auto& n : in.notes)
                    if (n.track == static_cast<int> (t))
                        notes.push_back (&n);

                if (notes.empty())
                {
                    role = TrackRole::ignore;
                }
                else
                {
                    // Monophonic: notes rarely overlap. Then the register decides.
                    std::sort (notes.begin(), notes.end(), [] (auto* a, auto* b) { return a->start < b->start; });
                    double total = 0.0, overlapped = 0.0;
                    for (std::size_t i = 0; i < notes.size(); ++i)
                    {
                        total += notes[i]->length;
                        if (i + 1 < notes.size())
                            overlapped += overlap (notes[i]->start, notes[i]->start + notes[i]->length,
                                                   notes[i + 1]->start, notes[i + 1]->start + notes[i + 1]->length);
                    }
                    // A melody moves mostly by step; a broken-chord (arpeggio)
                    // part mostly leaps, and is a chord part, not a melody.
                    int steps = 0, moves = 0;
                    for (std::size_t i = 1; i < notes.size(); ++i)
                    {
                        const int d = std::abs (notes[i]->pitch - notes[i - 1]->pitch);
                        if (d == 0)
                            continue;
                        ++moves;
                        steps += d <= 2 ? 1 : 0;
                    }
                    const bool stepwise = moves == 0 || steps * 2 >= moves;

                    std::vector<int> pitches;
                    for (auto* n : notes)
                        pitches.push_back (n->pitch);
                    std::nth_element (pitches.begin(), pitches.begin() + static_cast<std::ptrdiff_t> (pitches.size() / 2), pitches.end());
                    const int median = pitches[pitches.size() / 2];
                    const bool mono = overlapped < 0.1 * total;

                    role = ! mono ? TrackRole::chords
                         : median < 52 ? TrackRole::bass
                         : median >= 60 && stepwise ? TrackRole::melody
                                                    : TrackRole::chords;
                }
            }
            roles.push_back (role);
        }
        return roles;
    }

    // ---- 2. embellishments -------------------------------------------------------------

    struct Weighted
    {
        double start, end;
        int pitch;
        double weight;   // velocity x role
        TrackRole role;
        bool onset;      // starts here (not moved / carried)
        bool passing;    // looks like a passing / neighbour note: kept only if it fits the chord
    };

    std::vector<Weighted> clean (const ChordInput& in, const std::vector<TrackRole>& roles, const ChordSettings& s)
    {
        std::vector<Weighted> out;

        for (std::size_t t = 0; t < roles.size(); ++t)
        {
            const auto role = roles[t];
            if (role == TrackRole::ignore || role == TrackRole::automatic)
                continue;

            std::vector<ChordNote> notes;
            for (const auto& n : in.notes)
                if (n.track == static_cast<int> (t) && n.length > kEps)
                    notes.push_back (n);
            std::sort (notes.begin(), notes.end(), [] (const ChordNote& a, const ChordNote& b) { return a.start < b.start; });

            std::vector<bool> drop (notes.size(), false), passing (notes.size(), false);
            std::vector<double> startOf (notes.size());
            for (std::size_t i = 0; i < notes.size(); ++i)
                startOf[i] = notes[i].start;

            if (s.filterEmbellishments)
            {
                // Played notes start a little early or late and are held for
                // uneven lengths, so the rules look at where notes START, with
                // some tolerance, never at where they end.
                constexpr double loose = 0.15;
                auto stepTo = [] (int a, int b) { const int d = std::abs (a - b); return d >= 1 && d <= 2; };
                // Notes are sorted by start: look only at the window that matters.
                auto firstFrom = [&notes] (double from)
                {
                    return static_cast<std::size_t> (std::lower_bound (notes.begin(), notes.end(), from,
                        [] (const ChordNote& n, double v) { return n.start < v; }) - notes.begin());
                };
                auto startsTogether = [&] (std::size_t i)   // part of a chord (a stab), not a lone note
                {
                    for (std::size_t j = firstFrom (notes[i].start - kTouch); j < notes.size() && notes[j].start <= notes[i].start + kTouch; ++j)
                        if (j != i)
                            return true;
                    return false;
                };
                // A note starting in (from, to] a step away from note i.
                auto stepBetween = [&] (std::size_t i, double from, double to)
                {
                    for (std::size_t j = firstFrom (from); j < notes.size() && notes[j].start <= to; ++j)
                        if (j != i && notes[j].start > from && stepTo (notes[j].pitch, notes[i].pitch))
                            return static_cast<int> (j);
                    return -1;
                };

                for (std::size_t i = 0; i < notes.size(); ++i)
                {
                    const auto& n = notes[i];
                    const bool alone = ! startsTogether (i);

                    // Grace notes: very short and leading straight into a note a step away.
                    if (n.length <= 0.13 && stepBetween (i, n.start - 0.03, n.start + 0.3) >= 0)
                    {
                        drop[i] = true;
                        continue;
                    }
                    // Fast runs: short single notes off the beat (chord stabs stay).
                    if (alone && n.length < 0.25 + kEps && ! onBeat (n.start))
                    {
                        drop[i] = true;
                        continue;
                    }

                    // Passing / neighbour notes: short, off the beat, a step from a
                    // note just before and a note just after. Only a candidate:
                    // pass 2 keeps it if it belongs to the chord (a broken add9
                    // chord moves by step too).
                    if (alone && n.length <= 0.6 && ! onBeat (n.start)
                        && stepBetween (i, n.start - 1.0, n.start - 0.05) >= 0
                        && stepBetween (i, n.start + 0.05, n.start + 1.0) >= 0)
                    {
                        passing[i] = true;
                        continue;
                    }

                    // Appoggiatura / suspension inside a chord: a chord note on the
                    // beat (up to two beats long) followed, while the rest of the
                    // chord holds on, by a note a step away. In a chord change
                    // every voice moves, so that is not a suspension. Its weight
                    // goes to the note it resolves to.
                    if (role != TrackRole::chords || ! onBeat (n.start) || n.length > 2.0 + loose)
                        continue;
                    const double end = n.start + n.length;
                    const int r = stepBetween (i, n.start + 0.25, end + loose);
                    if (r < 0)
                        continue;
                    // A real resolution lasts about as long as the note it resolves
                    // (G-F# in Dsus4-D); a quick note after a chord tone is an
                    // ornament of that chord tone, not its resolution.
                    const auto& res = notes[static_cast<std::size_t> (r)];
                    if (res.length < 0.75 * n.length - 0.5 * loose)
                        continue;
                    int with = 0;
                    bool held = false;
                    // Notes sounding at its start began at most a few bars before.
                    for (std::size_t j = firstFrom (n.start - 16.0); j < notes.size() && notes[j].start <= n.start + loose; ++j)
                    {
                        if (j == i || static_cast<int> (j) == r)
                            continue;
                        const auto& m = notes[j];
                        if (m.start <= n.start + loose && m.start + m.length > n.start + loose)
                        {
                            ++with;
                            held = held || m.start + m.length > res.start + loose;
                        }
                    }
                    if (with >= 2 && held)
                    {
                        drop[i] = true;
                        startOf[static_cast<std::size_t> (r)] = std::min (startOf[static_cast<std::size_t> (r)], n.start);
                    }
                }
            }

            const double roleWeight = role == TrackRole::melody ? 0.25 : 1.0;
            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                if (drop[i])
                    continue;
                const auto& n = notes[i];
                const double velocity = 0.75 + 0.25 * std::clamp (n.velocity, 1, 127) / 127.0;
                const double end = n.start + n.length;
                out.push_back ({ startOf[i], end, n.pitch, roleWeight * velocity, role, true, passing[i] });

                // Chord notes ring on a little (pedal-like), at half weight, so
                // broken chords add up within a beat or two; like a pedal
                // changed on the new chord, not past the next bar line. Quick
                // ornaments and passing notes don't.
                if (role == TrackRole::chords && s.carryBeats > kEps && n.length >= 0.4 && ! passing[i])
                {
                    const double nextBar = std::floor ((n.start + kTouch) / s.barBeats + 1.0) * s.barBeats;
                    const double carryEnd = std::min (end + s.carryBeats, std::max (end, nextBar));
                    if (carryEnd > end + kEps)
                        out.push_back ({ end, carryEnd, n.pitch, 0.5 * roleWeight * velocity, role, false, passing[i] });
                }
            }
        }
        return out;
    }

    // ---- 3. & 4. slices ---------------------------------------------------------------------

    struct Slice
    {
        double start = 0.0, end = 0.0;
        std::array<double, 12> weight {};
        double total = 0.0;
        int bass = -1;
    };

    double metric (double t, const ChordSettings& s)
    {
        const double inBar = std::fmod (std::fmod (t, s.barBeats) + s.barBeats, s.barBeats);
        if (inBar < kTouch || s.barBeats - inBar < kTouch)
            return 1.2;   // the downbeat
        if (onBeat (t))
            return 1.0;
        if (std::abs (t * 2.0 - std::round (t * 2.0)) < kTouch)
            return 0.8;   // an off-beat eighth
        return 0.6;
    }

    double registerWeight (int pitch, TrackRole role)
    {
        if (role == TrackRole::bass)
            return 1.2;
        if (pitch >= 84)
            return 0.5;   // high decoration
        if (pitch < 48)
            return 1.15;
        return 1.0;
    }

    std::vector<Slice> slice (const std::vector<Weighted>& notes, double songEnd, const ChordSettings& s)
    {
        std::vector<Slice> slices;
        const auto count = static_cast<std::size_t> (std::ceil (std::max (0.0, songEnd) / s.sliceBeats - kEps));
        for (std::size_t k = 0; k < count; ++k)
        {
            Slice sl;
            sl.start = static_cast<double> (k) * s.sliceBeats;
            sl.end = sl.start + s.sliceBeats;

            double bestBass = 0.0;
            int lowestPitch = 1000;
            double lowestLen = 0.0;
            for (const auto& n : notes)
            {
                const double o = overlap (n.start, n.end, sl.start, sl.end);
                if (o <= kEps)
                    continue;
                const double m = n.onset ? (n.start >= sl.start - kEps ? metric (n.start, s) : 1.0) : 1.0;
                const double w = o * m * registerWeight (n.pitch, n.role) * n.weight;
                sl.weight[static_cast<std::size_t> (pc (n.pitch))] += w;
                sl.total += w;

                // The bass: the bass-track note sounding longest in the slice;
                // without a bass track, the lowest chord note that sounds (or
                // rings on) for a good part of it.
                // (A note struck on the beat outweighs a walking bass's
                // passing note of the same length.)
                if (const double b = o * (onBeat (n.start) ? 1.5 : 1.0);
                    n.role == TrackRole::bass && n.onset && b > bestBass + kEps)
                {
                    bestBass = b;
                    sl.bass = pc (n.pitch);
                }
                if (n.role == TrackRole::chords && o >= 0.4 * s.sliceBeats - kEps
                    && (n.pitch < lowestPitch || (n.pitch == lowestPitch && o > lowestLen)))
                {
                    lowestPitch = n.pitch;
                    lowestLen = o;
                }
            }
            if (sl.bass < 0 && lowestPitch < 1000)
                sl.bass = pc (lowestPitch);
            slices.push_back (sl);
        }
        return slices;
    }

    // ---- key ------------------------------------------------------------------------------

    Key estimateKey (const std::vector<Slice>& slices)
    {
        static const double major[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double minor[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        std::array<double, 12> hist {};
        for (const auto& sl : slices)
            for (std::size_t i = 0; i < 12; ++i)
                hist[i] += sl.weight[i];

        auto correlate = [&hist] (const double* profile, int tonic)
        {
            double mh = 0.0, mp = 0.0;
            for (int i = 0; i < 12; ++i) { mh += hist[static_cast<std::size_t> (i)]; mp += profile[i]; }
            mh /= 12.0; mp /= 12.0;
            double num = 0.0, dh = 0.0, dp = 0.0;
            for (int i = 0; i < 12; ++i)
            {
                const double h = hist[static_cast<std::size_t> ((i + tonic) % 12)] - mh;
                const double p = profile[i] - mp;
                num += h * p; dh += h * h; dp += p * p;
            }
            return dh > 0.0 && dp > 0.0 ? num / std::sqrt (dh * dp) : 0.0;
        };

        Key best;
        double bestScore = -2.0;
        for (int t = 0; t < 12; ++t)
        {
            if (const double c = correlate (major, t); c > bestScore) { bestScore = c; best = { t, false }; }
            if (const double c = correlate (minor, t); c > bestScore) { bestScore = c; best = { t, true }; }
        }
        return best;
    }

    bool inKey (const Key& key, int pitchClass)
    {
        // Major scale, or natural minor plus the raised 7th (harmonic minor's V).
        static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10, 11 };
        const int rel = pc (pitchClass - key.tonic);
        if (key.minor)
            return std::find (std::begin (minorSteps), std::end (minorSteps), rel) != std::end (minorSteps);
        return std::find (std::begin (majorSteps), std::end (majorSteps), rel) != std::end (majorSteps);
    }

    // ---- 5. scoring ------------------------------------------------------------------------

    double score (const Slice& sl, int root, const QualityInfo& q, const Key& key)
    {
        if (sl.total <= kEps)
            return 0.0;

        double maxW = 0.0;
        for (const double w : sl.weight)
            maxW = std::max (maxW, w);

        std::array<bool, 12> member {};
        double in = 0.0, missing = 0.0;
        bool diatonic = true;
        for (const int interval : q.intervals)
        {
            const auto p = static_cast<std::size_t> (pc (root + interval));
            member[p] = true;
            const double imp = importance (q.quality, interval);
            in += sl.weight[p];   // every chord tone that sounds counts alike; importance is for missing ones
            // An extension (6th, 7th, 9th) has to be really there to make the
            // chord richer, not a trace left by an ornament.
            const bool extension = interval == 9 || interval == 10 || interval == 11
                                   || (interval == 2 && q.quality != Quality::sus2);
            if (sl.weight[p] < (extension ? 0.3 : 0.08) * maxW)
                missing += interval == 7 ? 0.15 : imp;   // a missing third or sus tone hurts most
            diatonic = diatonic && inKey (key, static_cast<int> (p));
        }
        double out = 0.0;
        for (std::size_t p = 0; p < 12; ++p)
            if (! member[p])
                out += sl.weight[p];

        double s = (in - 0.8 * out) / sl.total
                   - 0.35 * missing
                   - q.complexity;

        // The bass note: the strongest single clue to the root, but an
        // inversion (C/G, Bb/F) is common enough not to lose to a chord built
        // on the bass that is missing its third.
        if (sl.bass >= 0)
        {
            const int rel = pc (sl.bass - root);
            if (rel == 0)                                 s += 0.25;
            else if (rel == 3 || rel == 4)                s += 0.12;   // first inversion
            else if (rel == 7)                            s += 0.10;   // second inversion
            else if (member[static_cast<std::size_t> (sl.bass)]) s += 0.06;
            else                                          s -= 0.10;   // a bass outside the chord
        }
        if (diatonic)
            s += 0.04;
        return s;
    }

    // ---- 6. Viterbi --------------------------------------------------------------------------

    double changeCost (double t, const ChordSettings& s)
    {
        const double inBar = std::fmod (std::fmod (t, s.barBeats) + s.barBeats, s.barBeats);
        if (inBar < kTouch || s.barBeats - inBar < kTouch)
            return s.barChangeCost;
        const double half = s.barBeats / 2.0;
        if (std::abs (inBar - half) < kTouch)
            return s.halfBarChangeCost;
        return s.changeCost;
    }
}

namespace
{
    struct Pass
    {
        std::vector<Slice> slices;
        std::vector<std::vector<double>> emission;
        std::vector<std::size_t> path;
    };

    std::size_t stateCount() { return 12 * qualities().size() + 1; }

    Chord stateChord (std::size_t st)
    {
        const auto& qs = qualities();
        Chord c;
        if (st >= 12 * qs.size())
            return c;   // no chord
        c.root = static_cast<int> (st / qs.size());
        c.quality = qs[st % qs.size()].quality;
        return c;
    }

    Pass decode (const std::vector<Weighted>& notes, double songEnd, const ChordSettings& settings, const Key* knownKey, Key& key)
    {
        Pass pass;
        pass.slices = slice (notes, songEnd, settings);
        key = knownKey != nullptr ? *knownKey : estimateKey (pass.slices);
        const auto& slices = pass.slices;
        if (slices.empty())
            return pass;

        const auto& qs = qualities();
        const std::size_t states = stateCount();
        const std::size_t noChord = states - 1;
        const std::size_t n = slices.size();

        auto& emission = pass.emission;
        emission.assign (n, std::vector<double> (states, 0.0));
        for (std::size_t k = 0; k < n; ++k)
        {
            for (std::size_t st = 0; st < noChord; ++st)
                emission[k][st] = score (slices[k], static_cast<int> (st / qs.size()), qs[st % qs.size()], key);
            // Silence: "no chord" is slightly better than any chord, so a rest
            // at the start stays empty, while a rest of up to a few bars mid-song
            // keeps the chord (changing away and back costs more).
            emission[k][noChord] = slices[k].total <= kEps ? 0.02 : -0.5;
        }

        std::vector<std::vector<double>> best (n, std::vector<double> (states, 0.0));
        std::vector<std::vector<std::size_t>> from (n, std::vector<std::size_t> (states, 0));
        best[0] = emission[0];
        // A change costs more mid-bar, and more again when it keeps the root (a
        // variant of the same chord: a line moving on top, not a new chord).
        // Per state: the best previous chord on another root, and on the same root.
        for (std::size_t k = 1; k < n; ++k)
        {
            const double cost = changeCost (slices[k].start, settings);
            const auto& prev = best[k - 1];
            const std::size_t perRoot = qs.size();

            // Best and second-best previous state, by root group (12 roots + no chord).
            std::vector<std::size_t> rootBest (13, noChord), rootSecond (13, noChord);
            std::vector<double> rootBestScore (13, -1e18), rootSecondScore (13, -1e18);
            for (std::size_t st = 0; st < states; ++st)
            {
                const std::size_t r = st == noChord ? 12 : st / perRoot;
                if (prev[st] > rootBestScore[r])
                {
                    rootSecondScore[r] = rootBestScore[r]; rootSecond[r] = rootBest[r];
                    rootBestScore[r] = prev[st];           rootBest[r] = st;
                }
                else if (prev[st] > rootSecondScore[r])
                {
                    rootSecondScore[r] = prev[st];         rootSecond[r] = st;
                }
            }
            // The two best root groups overall.
            std::size_t g1 = 0, g2 = 1;
            if (rootBestScore[g2] > rootBestScore[g1]) std::swap (g1, g2);
            for (std::size_t r = 2; r < 13; ++r)
            {
                if (rootBestScore[r] > rootBestScore[g1])      { g2 = g1; g1 = r; }
                else if (rootBestScore[r] > rootBestScore[g2]) g2 = r;
            }

            for (std::size_t st = 0; st < states; ++st)
            {
                const std::size_t r = st == noChord ? 12 : st / perRoot;
                double bestScore = prev[st];   // staying: free
                std::size_t bestFrom = st;
                // From another root.
                const std::size_t other = g1 != r ? g1 : g2;
                // (On a tie, only in silence where every chord scores the same,
                // the change comes as late as possible: a chord lasts until the
                // next one sounds. So a move wins ties here.)
                if (const double v = rootBestScore[other] - cost; v > bestScore - 1.0e-9 && other != r)
                {
                    bestScore = v;
                    bestFrom = rootBest[other];
                }
                // From the same root, another quality.
                if (r < 12)
                {
                    const std::size_t same = rootBest[r] != st ? rootBest[r] : rootSecond[r];
                    const double sameScore = rootBest[r] != st ? rootBestScore[r] : rootSecondScore[r];
                    if (same != noChord && sameScore - cost - settings.sameRootChangeCost > bestScore + 1.0e-9)
                    {
                        bestScore = sameScore - cost - settings.sameRootChangeCost;
                        bestFrom = same;
                    }
                }
                best[k][st] = bestScore + emission[k][st];
                from[k][st] = bestFrom;
            }
        }

        pass.path.assign (n, 0);
        pass.path[n - 1] = static_cast<std::size_t> (std::max_element (best[n - 1].begin(), best[n - 1].end()) - best[n - 1].begin());
        for (std::size_t k = n - 1; k > 0; --k)
            pass.path[k - 1] = from[k][pass.path[k]];
        return pass;
    }
}

ChordResult detectChords (const ChordInput& input, double songEnd, const ChordSettings& given)
{
    // A grid of nothing (or less) can't be walked, and a song end no song has
    // (a damaged .als or .mid) would be a grid without end: every slice costs
    // a few kB in the Viterbi tables.
    constexpr double kMaxSlices = 100000.0;
    ChordSettings settings = given;
    if (! (settings.sliceBeats > 0.0))  settings.sliceBeats = 1.0;
    if (! (settings.barBeats > 0.0))    settings.barBeats = 4.0;
    songEnd = std::min (songEnd, kMaxSlices * settings.sliceBeats);

    // Notes below the floor (sound selectors / keyswitches) are left out before
    // anything else, so they can't change a track's role either.
    ChordInput in;
    in.tracks = input.tracks;
    in.notes.reserve (input.notes.size());
    for (const auto& n : input.notes)
        if (n.pitch >= settings.lowestPitch)
            in.notes.push_back (n);

    ChordResult result;
    result.roles = decideRoles (in);
    auto notes = clean (in, result.roles, settings);

    // Pass 1 with the possible passing notes at a third of their weight (so
    // they can't pull the chord their way); pass 2 with only the ones that
    // belong to the chord pass 1 found where they sound, at full weight.
    bool anyPassing = false;
    auto firstPass = notes;
    for (auto& note : firstPass)
        if (note.passing)
        {
            note.weight *= 0.3;
            anyPassing = true;
        }
    Key key;
    auto pass = decode (firstPass, songEnd, settings, nullptr, key);
    if (anyPassing && ! pass.path.empty())
    {
        std::vector<Weighted> kept;
        for (const auto& note : notes)
        {
            if (! note.passing)
            {
                kept.push_back (note);
                continue;
            }
            const auto k = static_cast<std::size_t> (std::clamp (std::floor ((note.start + 1.0e-3) / settings.sliceBeats), 0.0,
                                                                 static_cast<double> (pass.path.size() - 1)));
            const auto c = stateChord (pass.path[k]);
            const auto intervals = chordIntervals (c.quality);
            if (c.isChord() && std::find (intervals.begin(), intervals.end(), pc (note.pitch - c.root)) != intervals.end())
                kept.push_back (note);
        }
        notes = std::move (kept);
        pass = decode (notes, songEnd, settings, &key, key);
    }
    result.key = key;

    const auto& slices = pass.slices;
    const auto& emission = pass.emission;
    const auto& path = pass.path;
    const std::size_t n = slices.size();
    if (n == 0)
        return result;
    const std::size_t chordStates = stateCount() - 1;

    if (settings.explain)
        for (std::size_t k = 0; k < n; ++k)
        {
            ChordSliceInfo info;
            info.start = slices[k].start;
            for (std::size_t p = 0; p < 12; ++p)
                info.weight[p] = slices[k].weight[p];
            info.bass = slices[k].bass;
            std::vector<std::size_t> order (chordStates);
            for (std::size_t st = 0; st < chordStates; ++st)
                order[st] = st;
            std::partial_sort (order.begin(), order.begin() + 3, order.end(),
                               [&] (std::size_t a, std::size_t b) { return emission[k][a] > emission[k][b]; });
            for (std::size_t m = 0; m < 3; ++m)
                info.top.push_back ({ stateChord (order[m]), emission[k][order[m]] });
            result.slices.push_back (info);
        }

    // Spans of equal chords; the bass note per span is the one heard most.
    std::size_t k = 0;
    while (k < n)
    {
        std::size_t j = k;
        while (j + 1 < n && path[j + 1] == path[k])
            ++j;

        ChordSpan span;
        span.start = slices[k].start;
        span.end = slices[j].end;
        if (path[k] < chordStates)
        {
            span.chord = stateChord (path[k]);
            std::array<double, 12> bassTime {};
            for (std::size_t m = k; m <= j; ++m)
                if (slices[m].bass >= 0)
                    bassTime[static_cast<std::size_t> (slices[m].bass)] += slices[m].end - slices[m].start;
            const auto most = std::max_element (bassTime.begin(), bassTime.end());
            span.chord.bass = *most > 0.0 ? static_cast<int> (most - bassTime.begin()) : span.chord.root;
            // A bass that isn't in the chord at all: show the chord without it.
            const auto intervals = chordIntervals (span.chord.quality);
            if (std::find (intervals.begin(), intervals.end(), pc (span.chord.bass - span.chord.root)) == intervals.end())
                span.chord.bass = span.chord.root;

            // Confidence: the winner's margin over the best chord on another root.
            double margin = 0.0;
            for (std::size_t m = k; m <= j; ++m)
            {
                double second = -1e9;
                for (std::size_t st = 0; st < chordStates; ++st)
                    if (stateChord (st).root != span.chord.root)
                        second = std::max (second, emission[m][st]);
                margin += emission[m][path[k]] - second;
            }
            span.confidence = std::clamp (margin / static_cast<double> (j - k + 1) / 0.4, 0.0, 1.0);
            result.spans.push_back (span);
        }
        k = j + 1;
    }
    return result;
}

TrackRole roleFromName (const std::string& trackName)
{
    std::string n;
    for (const char ch : trackName)
        n += static_cast<char> (std::tolower (static_cast<unsigned char> (ch)));
    auto has = [&n] (const char* w) { return n.find (w) != std::string::npos; };

    if (n == "parts" || n == "length" || n == "structure" || n == "arrangement")
        return TrackRole::ignore;
    for (const char* w : { "drum", "perc", "kick", "snare", "hat", "beat", "click", "metro", "fx" })
        if (has (w))
            return TrackRole::ignore;
    if (has ("bass") || has ("bas ") || n == "bas" || has ("808"))
        return TrackRole::bass;
    for (const char* w : { "vox", "vocal", "voc", "zang", "melod", "topline", "lead", "guide", "lyric" })
        if (has (w))
            return TrackRole::melody;
    return TrackRole::automatic;
}

// ---- names ------------------------------------------------------------------------------

namespace
{
    bool prefersFlats (const Key& key)
    {
        // Keys written with flats: F Bb Eb Ab Db Gb major; D G C F Bb Eb minor.
        static const int flatMajors[] = { 5, 10, 3, 8, 1, 6 };
        static const int flatMinors[] = { 2, 7, 0, 5, 10, 3 };
        const auto& list = key.minor ? flatMinors : flatMajors;
        return std::find (std::begin (list), std::end (list), key.tonic) != std::end (list);
    }

    std::string noteName (int pitchClass, bool flats)
    {
        static const char* sharpNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        static const char* flatNames[]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        const auto i = static_cast<std::size_t> (pc (pitchClass));
        return flats ? flatNames[i] : sharpNames[i];
    }
}

std::string chordName (const Chord& c, const Key& key)
{
    if (! c.isChord())
        return "N.C.";
    // C major / A minor: the usual mix (Bb, Eb, Ab, but F#, C#).
    const bool neutral = (! key.minor && key.tonic == 0) || (key.minor && key.tonic == 9);
    auto name = [&] (int p)
    {
        if (neutral)
            return noteName (p, p == 10 || p == 3 || p == 8);
        return noteName (p, prefersFlats (key));
    };
    std::string out = name (c.root) + infoFor (c.quality).suffix;
    if (c.bass >= 0 && c.bass != c.root)
        out += "/" + name (c.bass);
    return out;
}

std::string keyName (const Key& key)
{
    const bool flats = prefersFlats (key) || (! key.minor && key.tonic == 0);
    return noteName (key.tonic, flats) + (key.minor ? " minor" : " major");
}

std::vector<int> chordIntervals (Quality q)
{
    return q == Quality::none ? std::vector<int> {} : infoFor (q).intervals;
}

bool parseChordName (const std::string& text, Chord& out)
{
    std::string s;
    for (const char ch : text)
        if (ch != ' ')
            s += ch;
    out = Chord();
    if (s == "-" || s == "N.C." || s == "NC")
        return true;
    if (s.empty() || s[0] < 'A' || s[0] > 'G')
        return false;

    auto letter = [] (char ch) { static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; return base[ch - 'A']; };
    std::size_t i = 0;
    int root = letter (s[i++]);
    if (i < s.size() && (s[i] == '#' || s[i] == 'b')) root += s[i++] == '#' ? 1 : -1;

    std::string rest = s.substr (i), bassPart;
    if (const auto slash = rest.find ('/'); slash != std::string::npos)
    {
        bassPart = rest.substr (slash + 1);
        rest = rest.substr (0, slash);
    }
    // Tensions in parentheses beyond the 7th are dropped: E(7b13) -> E7.
    std::string q;
    for (std::size_t k = 0; k < rest.size(); ++k)
    {
        if (rest[k] == '(' || rest[k] == ')')
            continue;
        q += rest[k];
    }

    struct Map { const char* text; Quality quality; };
    static const Map table[] = {
        { "m7b5", Quality::halfDim7 }, { "maj7", Quality::maj7 }, { "M7", Quality::maj7 },
        { "msus2", Quality::minAdd9 }, { "madd9", Quality::minAdd9 }, { "m9", Quality::min7 },
        { "dim7", Quality::dim7 }, { "m7", Quality::min7 }, { "m6", Quality::min6 },
        { "sus2", Quality::sus2 }, { "sus4", Quality::sus4 }, { "sus", Quality::sus4 },
        { "add9", Quality::add9 }, { "dim", Quality::dim }, { "aug", Quality::aug }, { "+", Quality::aug },
        { "13", Quality::dom7 }, { "11", Quality::dom7 }, { "9", Quality::dom7 }, { "7", Quality::dom7 },
        { "6", Quality::six }, { "5", Quality::power }, { "m", Quality::minor }, { "min", Quality::minor },
        { "", Quality::major },
    };
    Quality quality = Quality::none;
    for (const auto& m : table)
    {
        const std::string t = m.text;
        if (q == t || (! t.empty() && q.rfind (t, 0) == 0 && t != "m" && t != "min" && t != "5" && t != "6"))
        {
            quality = m.quality;
            break;
        }
    }
    if (quality == Quality::none)
    {
        // "7b13", "7#9", "m7add11": the part we track is the start.
        if (q.rfind ("m7", 0) == 0) quality = Quality::min7;
        else if (q.rfind ("7", 0) == 0) quality = Quality::dom7;
        else if (q.rfind ("m", 0) == 0) quality = Quality::minor;
        else return false;
    }

    out.root = pc (root);
    out.quality = quality;
    out.bass = out.root;
    if (! bassPart.empty())
    {
        if (bassPart[0] < 'A' || bassPart[0] > 'G')
            return false;
        int b = letter (bassPart[0]);
        if (bassPart.size() > 1 && (bassPart[1] == '#' || bassPart[1] == 'b'))
            b += bassPart[1] == '#' ? 1 : -1;
        out.bass = pc (b);
    }
    return true;
}

}  // namespace boothcue
