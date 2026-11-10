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

    if (s.hasLyrics && settings.showLyrics)
    {
        s.currentCue  = song.cueAt (t.ppq);
        s.nextCue     = song.nextCueAfter (t.ppq);
        s.currentLine = lineText (song, s.currentCue);
        s.nextLine    = lineText (song, s.nextCue);
        if (s.nextCue >= 0)
            s.beatsToNextLine = song.cues()[static_cast<std::size_t> (s.nextCue)].ppq - t.ppq;
        s.countdown = countdownAt (song, t.ppq, s.sig, settings.count);
    }
    return s;
}

}  // namespace boothcue
