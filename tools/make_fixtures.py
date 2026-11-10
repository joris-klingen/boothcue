#!/usr/bin/env python3
"""Writes the synthetic .als fixtures used by als-check into tests/fixtures/.

The XML mirrors what Live writes where the parser looks (see Source/AlsParser.h
and bbb_code's bbb/als.py), plus decoys the parser must NOT pick up: session
clips, take-lane clips, device names, a group track, and clips on other tracks.
Re-run after editing; the output is deterministic.
"""
import gzip
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'tests', 'fixtures')


def midi_clip(i, start, end, name, tag='MidiClip', notes=None, loop=None, disabled=False):
    """notes: [(time, duration, pitch, velocity, enabled)] in clip content time.
    loop: (loop_start, loop_end, start_relative, loop_on); default: the whole clip, off."""
    if notes is None:
        notes = [(0, 1, 60, 100, True)]
    loop_start, loop_end, start_rel, loop_on = loop if loop else (0, end - start, 0, False)
    keys = {}
    for t, d, p, v, on in notes:
        keys.setdefault(p, []).append(
            f'<MidiNoteEvent Time="{t}" Duration="{d}" Velocity="{v}" OffVelocity="64" NoteId="1" '
            f'IsEnabled="{"true" if on else "false"}" />')
    key_tracks = ''.join(
        f'<KeyTrack Id="{n}"><Notes>{"".join(evs)}</Notes><MidiKey Value="{p}" /></KeyTrack>'
        for n, (p, evs) in enumerate(sorted(keys.items())))
    return f'''<{tag} Id="{i}" Time="{start}">
  <LomId Value="0" />
  <CurrentStart Value="{start}" />
  <CurrentEnd Value="{end}" />
  <Loop>
    <LoopStart Value="{loop_start}" />
    <LoopEnd Value="{loop_end}" />
    <StartRelative Value="{start_rel}" />
    <LoopOn Value="{"true" if loop_on else "false"}" />
  </Loop>
  <Name Value="{name}" />
  <Color Value="{i}" />
  <Disabled Value="{"true" if disabled else "false"}" />
  <Notes>
    <KeyTracks>
      {key_tracks}
    </KeyTracks>
  </Notes>
</{tag}>'''


def track(tag, i, user, effective, clips=(), group=-1, session=(), lanes=(), muted=False, drums=False):
    seq = 'ClipTimeable' if tag == 'MidiTrack' else 'Sample'
    return f'''<{tag} Id="{i}">
  <LomId Value="0" />
  <Name>
    <EffectiveName Value="{effective}" />
    <UserName Value="{user}" />
    <Annotation Value="" />
  </Name>
  <Color Value="12" />
  <TrackGroupId Value="{group}" />
  <TakeLanes>
    <TakeLanes>
      <TakeLane Id="0">
        <Name Value="Take 1" />
        <ClipAutomation><Events>{''.join(lanes)}</Events></ClipAutomation>
      </TakeLane>
    </TakeLanes>
  </TakeLanes>
  <DeviceChain>
    <Mixer>
      <Name><EffectiveName Value="Mixer" /><UserName Value="parts" /></Name>
      <Speaker><Manual Value="{"false" if muted else "true"}" /></Speaker>
    </Mixer>
    <MainSequencer>
      <ClipSlotList>
        <ClipSlot Id="0"><ClipSlot><Value>{''.join(session)}</Value></ClipSlot></ClipSlot>
      </ClipSlotList>
      <{seq}>
        <ArrangerAutomation>
          <Events>
{''.join(clips)}
          </Events>
        </ArrangerAutomation>
      </{seq}>
    </MainSequencer>
    <DeviceChain>
      <Devices>
        <PluginDevice Id="0">
          <Name><EffectiveName Value="parts" /><UserName Value="parts" /></Name>
        </PluginDevice>{'<DrumGroupDevice Id="1"><Branches /></DrumGroupDevice>' if drums else ''}
      </Devices>
    </DeviceChain>
  </DeviceChain>
</{tag}>'''


def live_set(tracks):
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<Ableton MajorVersion="5" MinorVersion="12.0_12049" Creator="Ableton Live 12.0.5">
  <!-- synthetic fixture, see tools/make_fixtures.py -->
  <LiveSet>
    <NextPointeeId Value="1000" />
    <Tracks>
{''.join(tracks)}
    </Tracks>
    <MainTrack>
      <Name><EffectiveName Value="Main" /><UserName Value="" /></Name>
      <DeviceChain><Mixer><Tempo><Manual Value="96" /></Tempo></Mixer></DeviceChain>
    </MainTrack>
    <Scenes />
  </LiveSet>
</Ableton>
'''


# name, bars (4/4). Deliberately written out of order below, with a gap before the Outro.
SONG = [('Intro', 4), ('Verse 1', 8), ('Chorus', 8), ('Verse 2', 8), ('Chorus', 8),
        ('Bridge &amp; &quot;Solo&quot;', 4.5), ('Chorus', 16)]


def parts_clips():
    clips, pos = [], 0.0
    for i, (name, bars) in enumerate(SONG):
        clips.append(midi_clip(i, pos, pos + bars * 4, name))
        pos += bars * 4
    clips.append(midi_clip(len(SONG), pos + 8, pos + 24, 'Outro'))   # 2-bar gap
    return clips[3:] + clips[:3]   # not in time order in the file


def write(name, xml, gz=True):
    path = os.path.join(OUT, name)
    data = xml.encode('utf-8')
    if gz:
        with open(path, 'wb') as raw, gzip.GzipFile(fileobj=raw, mode='wb', mtime=0, filename='') as f:
            f.write(data)
    else:
        with open(path, 'wb') as f:
            f.write(data)
    return path


os.makedirs(OUT, exist_ok=True)

decoy_session = [midi_clip(90, 0, 16, 'SessionDecoy')]
decoy_lane = [midi_clip(91, 0, 16, 'LaneDecoy')]

# 1. The normal case: "Parts" is the 2nd track (capital P, as in the BBB template).
normal = [
    track('MidiTrack', 10, 'length', 'length', [midi_clip(0, 0, 248, 'length')]),
    track('MidiTrack', 11, 'Parts', 'Parts', parts_clips(), session=decoy_session, lanes=decoy_lane),
    track('GroupTrack', 12, 'Vocals', 'Vocals'),
    track('AudioTrack', 13, 'Lead', 'Lead', [midi_clip(0, 0, 32, 'AudioDecoy', tag='AudioClip')], group=12),
    track('MidiTrack', 14, 'Keys', 'Keys', [midi_clip(0, 0, 64, 'KeysDecoy')]),
    track('ReturnTrack', 15, 'A-Reverb', 'A-Reverb'),
]
path = write('parts.als', live_set(normal))
write('parts-plain.xml', live_set(normal), gz=False)

# 2. "parts" is NOT the 2nd track, has no UserName (only EffectiveName), odd case and spaces.
moved = [
    track('AudioTrack', 10, 'Click', 'Click'),
    track('MidiTrack', 11, 'Keys', 'Keys', [midi_clip(0, 0, 64, 'KeysDecoy')]),
    track('MidiTrack', 12, '', ' PARTS ', [midi_clip(0, 0, 16, 'A'), midi_clip(1, 16, 48, 'B')]),
]
write('parts-moved.als', live_set(moved))

# 3. No track called parts: fall back on the 2nd track.
fallback = [
    track('MidiTrack', 10, 'length', 'length', [midi_clip(0, 0, 64, 'length')]),
    track('MidiTrack', 11, 'Structure', 'Structure', [midi_clip(0, 0, 32, 'One'), midi_clip(1, 32, 64, 'Two')]),
    track('MidiTrack', 12, 'Keys', 'Keys', [midi_clip(0, 0, 64, 'KeysDecoy')]),
]
write('fallback.als', live_set(fallback))

# 4. One track only, not called parts: nothing to use.
write('no-parts.als', live_set([track('MidiTrack', 10, 'Keys', 'Keys', [midi_clip(0, 0, 64, 'KeysDecoy')])]))

# 5. A save caught halfway: the gzip stream stops inside the first track.
with open(path, 'rb') as f:
    whole = f.read()
with open(os.path.join(OUT, 'truncated.als'), 'wb') as f:
    f.write(whole[:400])

# 6. Notes for chord detection: every clip-timing rule once.
C, Am, F, G = (60, 64, 67), (57, 60, 64), (65, 69, 72), (67, 71, 74)
def chord(t, d, pitches, v=90):
    return [(t, d, p, v, True) for p in pitches]
keys_clips = [
    # A: plain, 0-8: C then Am, plus a deactivated note.
    midi_clip(0, 0, 8, 'A', notes=chord(0, 4, C) + chord(4, 4, Am) + [(2, 1, 61, 90, False)]),
    # B: looped 4-beat F G, played 8-16 (twice).
    midi_clip(1, 8, 16, 'B', notes=chord(0, 2, F) + chord(2, 2, G), loop=(0, 4, 0, True)),
    # C: not looped, start marker at 2, end marker at 8: content 0 is before the
    #    start, content 9 after the end; plays 16-24.
    midi_clip(2, 16, 24, 'C', notes=[(0, 1, 40, 90, True), (2, 4, 41, 90, True), (6, 4, 42, 90, True),
                                       (9, 1, 43, 90, True)], loop=(0, 8, 2, False)),
    # D: deactivated clip.
    midi_clip(3, 24, 32, 'D', notes=chord(0, 8, C), disabled=True),
    # E: loop 2-6, start marker 3 (2 + 1), played 32-40: 49@32 50@34, then
    #    48@35 49@36 50@38, then 48@39.
    midi_clip(4, 32, 40, 'E', notes=[(2, 0.5, 48, 90, True), (3, 0.5, 49, 90, True), (5, 0.5, 50, 90, True)],
              loop=(2, 6, 1, True)),
]
noted = [
    track('MidiTrack', 10, 'length', 'length', [midi_clip(0, 0, 40, 'length', notes=[])]),
    track('MidiTrack', 11, 'Parts', 'Parts', [midi_clip(0, 0, 16, 'Verse', notes=[]), midi_clip(1, 16, 32, 'Chorus', notes=[])]),
    track('MidiTrack', 12, 'Keys', 'Keys', keys_clips),
    track('MidiTrack', 13, 'Bass', 'Bass', [midi_clip(0, 0, 16, 'b', notes=[(0, 4, 36, 100, True), (4, 4, 33, 100, True),
                                                                          (8, 2, 41, 100, True), (10, 2, 43, 100, True),
                                                                          (12, 2, 41, 100, True), (14, 2, 43, 100, True)])]),
    track('MidiTrack', 14, 'Drums', 'Drums', [midi_clip(0, 0, 16, 'beat', notes=[(t, 0.25, 36, 100, True) for t in range(16)])],
          drums=True),
    track('MidiTrack', 15, 'Old keys', 'Old keys', [midi_clip(0, 0, 16, 'x', notes=chord(0, 16, (61, 65, 68)))], muted=True),
    track('AudioTrack', 16, 'Vox', 'Vox'),
]
write('notes.als', live_set(noted))

# 7. Not a Live set at all.
write('not-a-set.als', '<?xml version="1.0"?><Other><LiveSet><Tracks /></LiveSet></Other>')

print('wrote fixtures to', os.path.abspath(OUT))
