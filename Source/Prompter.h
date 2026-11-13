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
    // The four "show" toggles.
    bool showSongPart = true;    // the strip of the whole song's parts, and the ticker
    bool showLyrics   = true;    // current / next line and the count-in
    bool showChords   = true;
    bool showBassNote = true;

    float fontScale = 1.0f;      // 0.5 .. 2, on top of the size derived from the window
    // A line is shown this long before its first word, so it can be read
    // ahead (0 = at the word itself). Never more than half the time since the
    // line before it started; entries that get a count-in are announced by
    // the count instead.
    double leadBars = 1.0;
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
    std::string laterLine;         // the line after nextLine: one more row to read ahead
    double beatsToNextLine = 0.0;
    Countdown countdown;
    // lineAhead: currentLine is shown ahead of its start (DisplaySettings::
    // leadBars); it starts in beatsToLine, and lineRunUp goes 0 -> 1 over the
    // time it is shown ahead.
    bool   lineAhead = false;
    double beatsToLine = 0.0, lineRunUp = 0.0;

    // Chords (with showChords): names spelled in the song's key; with
    // showBassNote the bass note is shown on its own (and left out of the name).
    bool hasChords = false;
    std::string prevChord, chord, nextChord, bassNote;
    bool chordUnsure = false;      // a low-confidence detection
    double beatsToNextChord = 0.0;

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
