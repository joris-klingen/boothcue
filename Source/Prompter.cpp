#include "Prompter.h"

#include <algorithm>
#include <cmath>

namespace boothcue
{

namespace
{
    constexpr double kEps = 1.0e-6;

    const std::string& lineText (const Song& song, int cue)
    {
        static const std::string empty;
        if (cue < 0)
            return empty;
        const auto& c = song.cues()[static_cast<std::size_t> (cue)];
        return song.blocks()[static_cast<std::size_t> (c.block)].lines[static_cast<std::size_t> (c.line)].text;
    }
}

std::string describeDistance (double beats, TimeSig sig)
{
    const double bar = sig.barLength();
    beats = std::isfinite (beats) ? std::min (beats, 1.0e9) : 0.0;   // whatever the host's position was: a count that fits
    if (beats > bar + kEps)
    {
        const int n = static_cast<int> (std::ceil (beats / bar - kEps));
        return std::to_string (n) + (n == 1 ? " bar" : " bars");
    }
    if (beats > bar - kEps)
        return "1 bar";
    const int n = std::max (1, static_cast<int> (std::ceil (beats / sig.beatLength() - kEps)));
    return std::to_string (n) + (n == 1 ? " beat" : " beats");
}

PrompterScene buildScene (const Song& song, const TransportState& t, const DisplaySettings& settings)
{
    PrompterScene s;

    s.hasPosition = t.valid;
    s.playing     = t.valid && t.playing;
    s.recording   = t.valid && t.recording;
    s.ppq         = t.ppq;
    s.sig         = TimeSig { t.sigNum > 0 ? t.sigNum : 4, t.sigDen > 0 ? t.sigDen : 4 };
    s.barBeat     = barBeatAt (t.ppq, s.sig, t.hasLastBarStart, t.lastBarStart);

    s.loopShown = t.valid && t.loopEnd > t.loopStart + kEps;
    s.looping   = s.loopShown && t.looping;
    s.loopStart = t.loopStart;
    s.loopEnd   = t.loopEnd;
    s.loopPass  = s.looping ? t.loopPass : 0;

    s.hasSong = ! song.sections().empty();
    if (s.hasSong)
    {
        // From bar 1 (or the first section, if the song starts earlier) to the
        // end of the last section.
        s.timelineStart = std::min (0.0, song.start());
        s.timelineEnd   = std::max (song.end(), s.timelineStart + s.sig.barLength());
    }

    s.hasLyrics = ! song.cues().empty();

    if (! t.valid)
        return s;

    if (s.hasSong)
    {
        s.section     = song.sectionAt (t.ppq);
        s.nextSection = song.nextSectionAfter (t.ppq);
        if (s.nextSection >= 0)
            s.beatsToNextSection = song.sections()[static_cast<std::size_t> (s.nextSection)].start - t.ppq;
    }

    if (settings.showChords && ! song.chords().empty())
    {
        s.hasChords = true;
        const auto key = song.key();
        auto name = [&] (int index)
        {
            if (index < 0)
                return std::string();
            auto c = song.chords()[static_cast<std::size_t> (index)].chord;
            if (settings.showBassNote)
                c.bass = c.root;   // the bass is shown on its own
            return chordName (c, key);
        };
        const int now = song.chordAt (t.ppq);
        const int prev = song.previousChordBefore (t.ppq);
        const int next = song.nextChordAfter (t.ppq);
        s.chord = name (now);
        s.prevChord = name (prev);
        s.nextChord = name (next);
        if (next >= 0)
            s.beatsToNextChord = song.chords()[static_cast<std::size_t> (next)].start - t.ppq;
        if (now >= 0)
        {
            const auto& span = song.chords()[static_cast<std::size_t> (now)];
            s.chordUnsure = span.confidence < 0.15;
            if (settings.showBassNote && span.chord.bass >= 0)
            {
                Chord bassOnly;
                bassOnly.root = span.chord.bass;
                bassOnly.quality = Quality::major;
                s.bassNote = chordName (bassOnly, key);   // a plain note name
            }
        }
    }

    if (s.hasLyrics && settings.showLyrics)
    {
        s.currentCue  = song.cueAt (t.ppq);
        s.nextCue     = song.nextCueAfter (t.ppq);
        s.currentLine = lineText (song, s.currentCue);
        s.nextLine    = lineText (song, s.nextCue);
        if (s.nextCue >= 0)
            s.beatsToNextLine = song.cues()[static_cast<std::size_t> (s.nextCue)].ppq - t.ppq;
        s.countdown = countdownAt (song, t.ppq, s.sig, settings.count);

        // Read ahead: the next line takes the big position a little before
        // its first word. Not for an entry that is being counted in (the
        // count announces it, with the line under it).
        if (settings.leadBars > 0.0 && s.nextCue >= 0 && s.countdown.kind == Countdown::Kind::none)
        {
            const auto& cues = song.cues();
            const auto next = static_cast<std::size_t> (s.nextCue);
            double lead = settings.leadBars * s.sig.barLength();
            if (next > 0)
                lead = std::min (lead, 0.5 * (cues[next].ppq - cues[next - 1].ppq));
            if (lead > kEps && s.beatsToNextLine <= lead + kEps)
            {
                s.lineAhead   = true;
                s.beatsToLine = s.beatsToNextLine;
                s.currentCue  = s.nextCue;
                s.nextCue     = next + 1 < cues.size() ? s.nextCue + 1 : -1;
                s.currentLine = lineText (song, s.currentCue);
                s.nextLine    = lineText (song, s.nextCue);
                s.beatsToNextLine = s.nextCue >= 0 ? cues[next + 1].ppq - t.ppq : 0.0;
            }
        }
        if (s.nextCue >= 0 && static_cast<std::size_t> (s.nextCue) + 1 < song.cues().size())
            s.laterLine = lineText (song, s.nextCue + 1);
    }
    return s;
}

}  // namespace boothcue
