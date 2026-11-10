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


def midi_clip(i, start, end, name, tag='MidiClip'):
    return f'''<{tag} Id="{i}" Time="{start}">
  <LomId Value="0" />
  <CurrentStart Value="{start}" />
  <CurrentEnd Value="{end}" />
  <Loop>
    <LoopStart Value="0" />
    <LoopEnd Value="{end - start}" />
    <LoopOn Value="false" />
  </Loop>
  <Name Value="{name}" />
  <Color Value="{i}" />
  <Notes>
    <KeyTracks>
      <KeyTrack Id="0">
        <Notes><MidiNoteEvent Time="0" Duration="1" Velocity="100" /></Notes>
        <MidiKey Value="60" />
      </KeyTrack>
    </KeyTracks>
  </Notes>
</{tag}>'''


def track(tag, i, user, effective, clips=(), group=-1, session=(), lanes=()):
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
      <Speaker><Manual Value="true" /></Speaker>
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
        </PluginDevice>
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

# 6. Not a Live set at all.
write('not-a-set.als', '<?xml version="1.0"?><Other><LiveSet><Tracks /></LiveSet></Other>')

print('wrote fixtures to', os.path.abspath(OUT))
