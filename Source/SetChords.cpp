#include "SetChords.h"

#include <algorithm>

namespace boothcue
{

SetChords chordsFromSet (const AlsResult& set, double barBeats)
{
    SetChords out;
    ChordInput input;
    double end = 0.0;
    for (const auto& s : set.sections)
        end = std::max (end, s.end);

    for (const auto& t : set.midiTracks)
    {
        ChordTrack track;
        track.name = t.name;
        track.role = (t.muted || t.drums || t.index == set.trackIndex) ? TrackRole::ignore : roleFromName (t.name);
        const auto index = static_cast<int> (input.tracks.size());
        input.tracks.push_back (track);
        if (track.role == TrackRole::ignore)
            continue;
        for (const auto& n : t.notes)
        {
            input.notes.push_back ({ n.start, n.length, n.pitch, n.velocity, index });
            end = std::max (end, n.start + n.length);
        }
    }
    out.anyNotes = ! input.notes.empty();
    if (! out.anyNotes)
        return out;

    ChordSettings settings;
    settings.barBeats = barBeats > 0.0 ? barBeats : 4.0;
    out.result = detectChords (input, end, settings);

    for (std::size_t i = 0; i < input.tracks.size() && i < out.result.roles.size(); ++i)
    {
        const auto role = out.result.roles[i];
        if (role == TrackRole::chords)      out.chordTracks.push_back (input.tracks[i].name);
        else if (role == TrackRole::bass)   out.bassTracks.push_back (input.tracks[i].name);
        else if (role == TrackRole::melody) out.melodyTracks.push_back (input.tracks[i].name);
    }
    return out;
}

}  // namespace boothcue
