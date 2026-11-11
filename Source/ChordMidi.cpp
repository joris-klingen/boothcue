#include "ChordMidi.h"

#include <algorithm>
#include <cmath>

namespace boothcue
{

namespace
{
    constexpr int kTicks = 960;   // per quarter note

    double ticks (double beats) { return std::round (beats * kTicks); }
}

std::vector<int> chordVoicing (const Chord& c)
{
    std::vector<int> notes;
    if (! c.isChord())
        return notes;
    // Root position, the root between F3 and E4.
    int root = 53 + ((c.root - 5 + 12) % 12);
    for (const int i : chordIntervals (c.quality))
        notes.push_back (root + i);
    // The bass note in octave 2 (C2..B2), under everything.
    const int bass = c.bass >= 0 ? c.bass : c.root;
    notes.push_back (36 + ((bass % 12) + 12) % 12);
    return notes;
}

juce::MidiFile chordsToMidi (const std::vector<ChordSpan>& chords, const Key& key, TimeSig sig, double bpm,
                             const juce::String& trackName)
{
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60.0e6 / juce::jlimit (20.0, 400.0, bpm))), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (sig.num, sig.den), 0.0);

    for (const auto& span : chords)
    {
        if (! span.chord.isChord() || span.end <= span.start)
            continue;
        const double on = ticks (span.start), off = ticks (span.end);
        seq.addEvent (juce::MidiMessage::textMetaEvent (1, juce::String::fromUTF8 (chordName (span.chord, key).c_str())), on);
        for (const int n : chordVoicing (span.chord))
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (90)), on);
            // Off a tick early, so a repeated note doesn't overlap the next on.
            seq.addEvent (juce::MidiMessage::noteOff (1, n), std::max (on + 1.0, off - 1.0));
        }
    }
    seq.sort();
    seq.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (kTicks);
    file.addTrack (seq);
    return file;
}

bool writeChordsMidi (const juce::File& target, const std::vector<ChordSpan>& chords, const Key& key, TimeSig sig,
                      double bpm, const juce::String& trackName)
{
    const auto midi = chordsToMidi (chords, key, sig, bpm, trackName);
    target.deleteFile();
    juce::FileOutputStream out (target);
    return out.openedOk() && midi.writeTo (out, 1);
}

MidiChordImport chordsFromMidiFile (const juce::File& file, TimeSig sig)
{
    MidiChordImport result;
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
    {
        result.error = "not a MIDI file";
        return result;
    }
    const int tpq = midi.getTimeFormat();
    if (tpq <= 0)
    {
        result.error = "a MIDI file with SMPTE timing (not supported)";
        return result;
    }
    auto beats = [tpq] (double t) { return t / tpq; };

    // Chord names written as text events: taken as they are.
    std::vector<ChordSpan> named;
    double lastNoteOff = 0.0;
    ChordInput input;
    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        auto seq = *midi.getTrack (t);
        seq.updateMatchedPairs();
        ChordTrack track;
        track.name = "track " + std::to_string (t + 1);
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            if (m.isTrackNameEvent())
                track.name = m.getTextFromTextMetaEvent().toStdString();
            else if (m.isTextMetaEvent() && m.getMetaEventType() != 3)
            {
                ChordSpan span;
                if (parseChordName (m.getTextFromTextMetaEvent().trim().toStdString(), span.chord) && span.chord.isChord())
                {
                    span.start = beats (m.getTimeStamp());
                    span.confidence = 1.0;
                    named.push_back (span);
                }
            }
            else if (m.isNoteOn())
            {
                const auto off = seq.getTimeOfMatchingKeyUp (i);
                const double start = beats (m.getTimeStamp());
                const double end = off > m.getTimeStamp() ? beats (off) : start + 1.0;
                input.notes.push_back ({ start, end - start, m.getNoteNumber(), m.getVelocity(), static_cast<int> (input.tracks.size()) });
                lastNoteOff = std::max (lastNoteOff, end);
            }
        }
        input.tracks.push_back (track);
    }

    if (! named.empty())
    {
        std::stable_sort (named.begin(), named.end(), [] (const ChordSpan& a, const ChordSpan& b) { return a.start < b.start; });
        // Each name lasts until the next, or until its notes stop.
        for (std::size_t i = 0; i < named.size(); ++i)
        {
            double end = i + 1 < named.size() ? named[i + 1].start : lastNoteOff;
            double notesEnd = named[i].start;
            for (const auto& n : input.notes)
                if (std::abs (n.start - named[i].start) < 1.0e-3)
                    notesEnd = std::max (notesEnd, n.start + n.length);
            if (notesEnd > named[i].start + 1.0e-3)
                end = std::min (end, std::ceil (notesEnd * kTicks) / kTicks + 1.0 / kTicks);   // the tick taken off
            named[i].end = std::max (named[i].start + 1.0e-3, end);
        }
        result.chords = named;
        result.fromNames = true;
        result.ok = true;
        // A key for spelling: from the notes.
        ChordSettings settings;
        settings.barBeats = sig.barLength();
        result.key = detectChords (input, lastNoteOff, settings).key;
        return result;
    }

    if (input.notes.empty())
    {
        result.error = "no notes and no chord names in it";
        return result;
    }
    ChordSettings settings;
    settings.barBeats = sig.barLength();
    const auto detected = detectChords (input, lastNoteOff, settings);
    result.chords = detected.spans;
    result.key = detected.key;
    result.ok = true;
    return result;
}

}  // namespace boothcue
