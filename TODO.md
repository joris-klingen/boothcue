# BoothCue — plan and backlog

BoothCue is a JUCE plugin (VST3 + AU, macOS first) that shows a singer, on a
second screen, the lyrics and where we are in the song while tracking in
Ableton Live. This file is the plan; nothing here is built unless it is listed
under "Done".

## Done

- **`.als` parser** — `Source/AlsParser.{h,cpp}`, plain C++ + zlib, streaming.
  Finds the MIDI track named `parts` (case-insensitive; fallback: 2nd track)
  and returns its arrangement clips as sections (name, start, end in beats).
  Tested by `als-check` on synthetic sets; matches ALSmuse on them
  (`tools/alsmuse_crosscheck.py`). **Not yet run on a real Live set.**
- **Song model** — `Source/Song.{h,cpp}`: sections + lyric blocks -> cues,
  current / next line, count-in, bar/beat readout, tap / nudge / clear.
- **Lyric import** — `Source/LyricImport.{h,cpp}`: `[Header]` text -> blocks,
  text round trip, taps kept when the text is edited.
- **Transport hand-off** — `Source/Transport.h`: `TransportState`,
  `SharedTransport` (audio thread -> message thread, lock-free), `LoopCounter`.
- **Plugin scaffold** (step 1) — `juce_add_plugin(BoothCue)`: JUCE 8.0.15,
  VST3 + AU (macOS) + Standalone, harmeq identity (`Joris Klingen`,
  `com.jorisklingen.boothcue`, `Jokl` / `Bcue`), audio effect, no parameters.
  `PluginProcessor` passes audio through untouched and, also when bypassed,
  copies the playhead into `SharedTransport` with the loop-pass count. The
  editor is a temporary transport readout for checking a build in Live.
  The core is a static library `boothcue-core`; `-DBOOTHCUE_BUILD_PLUGIN=OFF`
  builds only the core and its checks.
  Verified on Linux: builds without warnings, pluginval strictness 10 passes.
  **Not yet verified on macOS** (AU, universal build): that is what CI's macOS
  job does (`.github/workflows/ci.yml`: build, ctest, pluginval, auval, zip).
- Tests: `ctest` runs `als-parser` and `song-logic`.

## v1 — still to build, in this order

Conventions: flashroll / hitnotedmx layout and style (`Source/`, `tools/`,
`installer/install.command`, `CLAUDE.md`, `STATUS.md`), harmeq's plugin
identity and CI. Namespace `boothcue`. The core (`AlsParser`, `Song`,
`LyricImport`, `Transport.h`) stays free of JUCE.

1. ~~Plugin scaffold~~ — done, see "Done" above.
2. **Display** — `PrompterView` (one paint function taking `Song`,
   `TransportState`, `DisplaySettings`, so it can be rendered without a host)
   and `DisplayWindow` (a `DocumentWindow` owned by the *processor*, so it
   stays open when Live closes the plugin editor; native title bar, resizable,
   `F` / double-click = fullscreen, `Esc` leaves it).
   - Timeline strip: sections as blocks proportional to length, names,
     playhead, current section highlighted, host loop range as a bracket.
   - Header: section name, "next: Chorus in 3 bars", bar.beat, PLAY / REC,
     "Pass 3" while looping.
   - Centre: current line as large as fits (wraps to at most 3 lines), next
     line smaller and grey. Count-in: bars ("2 bars", "1 bar") as a badge on
     the next line, the last bar as a huge 4-3-2-1.
   - Dark, high contrast, every size derived from the window height times a
     font-scale setting.
   - `tools/PrompterRender.cpp` (as flashroll's `staff-render`): renders a
     contact sheet of scenes to a PNG; also a CTest.
3. **Linking the set** — `SetWatcher`: a background thread that polls the
   linked file's modification time and size about once a second, re-parses
   when they have settled, and hands the `AlsResult` to the message thread.
   A failed parse keeps the last good structure. Editor: "Link set…" button
   (async `FileChooser`) and a status line ("Parts — 9 sections — 14:02:11",
   or the error, or "2nd track used: no track named parts").
4. **Editor** — settings window in Live: link set, open display, lyric text
   box (paste) + "Load file…" + "Apply", the line list with each line's
   position, tap controls, display settings.
5. **Tap-along** — a selected line in the list is the tap cursor. Tap (button
   in the editor; `T` or space in the display window, since Live swallows most
   keys in plugin editors) stamps the cursor line with the current PPQ via
   `Song::stamp` and moves the cursor on. If the playhead is in a section that
   sings another block, the cursor jumps to that block's first line. Selecting
   a line and tapping re-times just that line; nudge buttons move it by a
   quarter beat; "clear" returns it to the even spread.
6. **State** — one `ValueTree` written by `getStateInformation`: the `.als`
   path, the last parsed sections (so the display still works when the file is
   missing on another machine), lyric blocks with line offsets, display
   settings, display window open / bounds / fullscreen.
   `getStateInformation` reads a snapshot under a lock and
   `setStateInformation` stores the incoming state as that snapshot and raises
   a flag for the message-thread timer, because hosts call both off the
   message thread (flashroll does the same).
7. **Docs and release** — `README.md`, `CLAUDE.md`, `STATUS.md`,
   `installer/install.command` (VST3 + AU component).
8. **Real-set check** — run `als-check --dump` and
   `tools/alsmuse_crosscheck.py` on a real set of Joris's and commit a small
   real `.als` as a fixture.

### Display toggles

`DisplaySettings` carries four independent "show" toggles, all persisted:
**song part**, **lyrics**, **chords**, **bass note**. The first two belong to
v1; the last two are wired when chords land (below). With lyrics off and chords
on, the same window serves a keyboard or guitar player.

## Next: chords (previous / current / next)

Goal: the display also shows the previous, current and next chord, so it works
for keyboard and guitar tracking. Ideally BoothCue reads the MIDI in the linked
set, detects the chords and their timing, and shows them alongside the lyrics.

1. **Read the notes** — extend `AlsParser` to return, per MIDI track, its
   arrangement clips *with notes*:
   - `MidiClip/Notes/KeyTracks/KeyTrack`: pitch in `MidiKey@Value`, notes in
     `Notes/MidiNoteEvent` (`Time`, `Duration`, `IsEnabled`), times relative
     to the clip's content.
   - Map to song time with the clip's `Time`, `CurrentStart`, `CurrentEnd`
     and `Loop/{LoopStart, LoopEnd, StartRelative, LoopOn}`; looped clips
     repeat their content until `CurrentEnd`.
   - Skip muted tracks (`DeviceChain/Mixer/Speaker/Manual`) and deactivated
     clips. Keep the streaming design: notes are collected only for the
     tracks that are asked for.
   - The parser currently stops at the `parts` track; with chords on it has
     to read to `</Tracks>`.
2. **Pick the source tracks** — editor setting: "Chords from: auto / these
   tracks". Auto = MIDI tracks whose notes overlap in threes or more for a
   good part of the song, leaving out the `parts` and `length` helpers and
   drum tracks (a `DrumGroupDevice` in the device chain). "Bass from" works
   the same way and defaults to the lowest-playing monophonic track; without
   one, the bass note is the lowest note of the chord tracks.
3. **Detect** — new plain-C++ module `Chords.{h,cpp}` with a `chord-check`
   CTest:
   - Cut the song at every beat (half bars as a setting); per slice, a
     pitch-class weight = note duration inside the slice.
   - Score the slice against templates (maj, min, dim, aug, sus2, sus4, 7,
     maj7, m7, m7b5, dim7, 6, add9); bass note = lowest sounding pitch, which
     gives slash chords (C/E).
   - Merge equal neighbours, drop changes shorter than a beat, treat slices
     with fewer than two pitch classes as "no chord" (keeps the last one).
   - Spell roots from the song's key (`KeySignature` / scale information in
     the set if present, otherwise the best-fitting key of all the chords), so
     it reads Bb, not A#, in F.
   - Result: `ChordTrack` = sorted `{ start, end, root, quality, bass }`.
4. **Manual chords** — detection will be wrong sometimes, and audio-only
   songs have nothing to detect. The lyric text takes chord lines per section,
   e.g. `| Am | F | C G |`, one bar per cell, which override detection for
   that section. Stored in the plugin state like the lyrics.
5. **Show** — `Song` gets `chordAt` / `previousChord` / `nextChord`; the
   display gets a chord row: previous (small, dim), current (large), next
   (medium) with the bars/beats until the change, and the bass note under the
   current chord when "bass note" is on. A transpose / capo setting shifts
   what is shown for guitar.
6. **Limits to state up front** — audio tracks are not analysed (no audio
   chord detection in this plan); arpeggios and sparse voicings need the
   half-bar slice setting; tempo and time-signature changes inside a song are
   still not handled.

## Later

- Snap taps to a grid (off / half beat / beat) and a tap-latency offset.
- Section colours from the clips' Live colours.
- Time-signature changes inside a song.
- Windows build (needs a zlib for the parser; JUCE's can be used there).
- Out of scope for now: pitch display, meters, producer notes / talkback,
  comp sheet.
