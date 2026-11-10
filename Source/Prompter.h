#pragma once

#include "Song.h"
#include "Transport.h"

#include <string>

namespace boothcue
{

// What the singer's display shows, decided without JUCE so it can be tested:
// buildScene turns (Song, host position, settings) into a PrompterScene, and
// PrompterView only draws it.

struct DisplaySettings
{
    // The four "show" toggles. Chords and bass note are stored and offered in
    // the editor, but nothing draws them until chord detection lands (TODO.md,
    // "Next: chords").
    bool showSongPart = true;    // header (section, next section, bar.beat) + timeline
    bool showLyrics   = true;    // current / next line and the count-in
    bool showChords   = true;
    bool showBassNote = true;

    float fontScale = 1.0f;      // 0.5 .. 2, on top of the size derived from the window
    CountSettings count;
};

struct PrompterScene
{
    // Host position. hasPosition = false: no host transport (the Standalone, or
    // a host that hasn't run audio yet).
    bool hasPosition = false;
    bool playing = false, recording = false;
    double ppq = 0.0;
    TimeSig sig;
    BarBeat barBeat;

    // Host loop. loopShown: there is a loop range worth drawing (on or off).
    bool   loopShown = false, looping = false;
    double loopStart = 0.0, loopEnd = 0.0;
    int    loopPass  = 0;

    // Song structure. timelineStart/End: the beat range the timeline covers.
    bool   hasSong = false;
    int    section = -1, nextSection = -1;
    double beatsToNextSection = 0.0;
    double timelineStart = 0.0, timelineEnd = 0.0;

    // Lyrics. currentCue = the line being sung (-1 in a rest); nextCue = the
    // line after the current position, whether or not it is close.
    bool hasLyrics = false;
    int  currentCue = -1, nextCue = -1;
    std::string currentLine, nextLine;
    double beatsToNextLine = 0.0;
    Countdown countdown;

    // Feedback for whoever taps in the display window: shown briefly along the
    // bottom edge, small, so the singer isn't distracted. Set by the view.
    std::string tapNote;
    bool tapNoteOk = true;
};

PrompterScene buildScene (const Song&, const TransportState&, const DisplaySettings&);

// "3 bars", "1 bar", "2 beats", "1 beat": rounded UP, so the last bar before a
// change reads "1 bar" until its beats are counted.
std::string describeDistance (double beats, TimeSig);

}  // namespace boothcue
