# BoothCue — status & development log

A living reference for the current state, plus a changelog. Update it along
with non-trivial commits. The plan and open backlog are in [TODO.md](TODO.md).

## What's built and working

| Layer | File(s) | State |
|-------|---------|-------|
| Plugin plumbing | `PluginProcessor.{h,cpp}`, `Transport.h`, `CMakeLists.txt` | JUCE 8.0.15, VST3 + Standalone (no AU). Identity `Joris Klingen`, `com.jorisklingen.boothcue`, `Jokl` / `Bcue`. Stereo audio effect that passes audio through untouched; no parameters. `processBlock` (also when bypassed) copies the playhead into `SharedTransport` (seqlock, with a `LoopCounter` for loop passes); `positionAt` / `tapPosition` extrapolate from the last block. A 10 Hz message-thread timer applies watcher reports, state set off the message thread and the host's time signature. |
| `.als` reader | `AlsParser.{h,cpp}` | Streaming gzip + XML scanner (zlib). The `parts` MIDI track (case-insensitive; fallback: 2nd track) -> sections with name, start, end and clip colour index. With `readNotes`: every MIDI track's arrangement notes, clip loops unrolled, start / end markers, deactivated clips and notes skipped, muted tracks and Drum Racks marked. A 10 MB set reads with notes in ~0.5 s. Matches ALSmuse on the fixtures. |
| Set watching | `SetWatcher.{h,cpp}` | Background thread, polls size + mtime once a second, re-reads once a change has held still for a poll (no half-written files). Detects chords on the same thread. A failed read keeps the last good structure. |
| Chord detection | `Chords.{h,cpp}`, `SetChords.{h,cpp}` | Notes below MIDI 33 dropped first (sound selectors, keyswitches). Track roles from names (bass, vocal / melody, drums and helpers ignored) or the notes (polyphony, register, steps vs leaps). Embellishment filtering on note starts with tolerance; passing notes decided in two passes (pass-1 weight x0.3); suspensions resolving under a held voice; ring-on up to the bar line. Weights per beat slice; candidates scored with the bass as a prior, inversion bonuses, extension tones only above 30 % of the top weight; Krumhansl key estimate for spelling; Viterbi smoothing with cheaper changes on bars / half bars and an extra cost for same-root changes. All constants in `ChordSettings`. |
| Song model | `Song.{h,cpp}` | Sections, lyric blocks -> cues (a block is sung in every section with its name; Dutch and short aliases), untimed lines spread on the bar grid (bars, half, quarter), `@bar.beat.16th` starts, tap-along (`tapAt` with a cursor that follows the playhead), nudge / clear, count-in (`countdownAt`), chords with previous / current / next lookups, time signature. |
| Text formats | `LyricImport.{h,cpp}`, `ChordChart.{h,cpp}`, `ChordMidi.{h,cpp}` | Lyrics: headers with or without brackets, repeat markers, stanza matching, `x 2`, pickups, chord-chart lines set aside, titles, `@` timings, `#` comments; export with timings; `keepTimings` (LCS) on edits. Chord chart: `[part]` blocks, `|...|` bars, `.` `-` `%` `||: :||` `x2`, label lines for kinds of parts; export / import round-trips. MIDI: 960 tpq, tempo + time signature, a text event per chord, voicing + bass; import by names, else detection. |
| Display | `Prompter.{h,cpp}`, `PrompterPainter.{h,cpp}`, `PrompterView.{h,cpp}`, `DisplayWindow.{h,cpp}`, `Theme.h` | `buildScene` decides what's shown (four toggles, text size, count-in). Header (part, next part, bar.beat, PLAY / REC / STOP, loop pass), overview of the whole song, a 16-bar chord ticker (playhead at 25 %, part colours, current chord yellow, bass notes, lyric marks, loop), lyrics fitted one line first (ordinary lines fit a 10" iPad), count-in. Parts drawn in Live's 70-colour clip palette. Top-level window owned by the processor: F / double-click full screen, Esc, T / space tap, Backspace undo. |
| Editor | `PluginEditor.{h,cpp}` | Left: Live set (link, reload, unlink, status), singer display settings, tap-along settings, transport readout. Right: Lyrics tab (text, Load / Save / Apply / Revert, line list with Tap, Undo, nudge, Clear) and Chords tab (Drag MIDI, Load / Save, Apply / Revert / Undo / Use detected, chart editor). Drops: `.als` links, `.txt` goes to the open tab, `.mid` imports chords. |
| State | `PluginProcessor.cpp` | Saved with the Live set: the set path and last structure (with colours), chords (+ edited flag), lyrics and timings, display and tap settings, the display window's open state, placement and full screen, the editor size. |
| Tests / tools | `tools/*.cpp`, `tools/*.py` | CTests `als-parser`, `chord-detect`, `song-logic`, `prompter-render`, `set-watcher`, `plugin-smoke`. Real-set tools: `als-check --chords / --chart`, `prompter-render --set / --midi`. `make_fixtures.py` generates the `.als` fixtures, `alsmuse_crosscheck.py` compares with ALSmuse. |

## Verified

- Linux (CI and locally): all six CTests pass; the VST3 passes pluginval at
  strictness 10; the core builds and tests without JUCE's GUI dependencies.
- Real set "papa is even weg 3.3": sections match ALSmuse; chords in Bb minor
  (choruses Bbm | Ab | Eb7 | Bbm, verses Bbm | Ab | Eb | Bbm); all parts link
  to the lyrics once named.
- **Not yet verified:** the macOS build, behaviour in Live (state with the
  set, drag-out of MIDI, full screen restore), and an iPad as the display.

## Changelog

- **One choice per song** (after v0.1.0): a *Song* menu replaces *Link
  set...* / *Link song...*: recent songs, the songbook's songs, the two file
  choosers, Unlink. `SongLibrary` finds a set's song (the `project` in
  `song.json`, else the project folder's name) and a song's newest set, and
  remembers the songbook folder, the projects folder and the last ten songs
  for every set. Checked in `plugin-smoke`; not yet tried in Live.

- **Display rework** (after v0.1.0):
  - a line is shown ahead of its first word (`DisplaySettings::leadBars`,
    1 bar by default, at most half the time since the line before it; decided
    in `buildScene`, editor: *Show a line ahead by*), dimmer and with a run-up
    bar; a third lyric row (the line after the next);
  - no header (part name, next part, bar.beat, PLAY / REC, loop pass): from
    the top the parts, the ticker, the lyrics. The count-in is off unless
    switched on, and never sent to the phones;
  - ticker: one chord size for the whole song, repeated chords not written
    again, the bass note only where it differs from the root, bar lines as the
    only vertical lines, the parts as a band in full colour;
  - colours: Live's own clip palette (from `pushbase/colors.pyc` in Live
    12.4; the old values were sampled from a screenshot), and parts are drawn
    in that colour everywhere (a part that is over is veiled, not greyed).
  Checked with `song-check` (lead), `plugin-smoke` (state) and renders of a
  real set; not yet tried in Live.

- **Song folder** (after v0.1.0): *Link song...* follows `lyrics.txt` and
  `chords.txt` of a song's folder in the songbook (bovenburen_manager) and
  writes the tapped starts next to them as `timings.txt`; saved with the set.
  `keepStarts` in `LyricImport` lays saved starts over a text. Checked in
  `song-check` and `plugin-smoke` on Linux; not yet tried in Live.

- **after v0.1.0** (review fixes):
  - `.als` reader: a file that ends inside the track list is a failed read
    (it used to come back as the 2nd track, or with tracks missing); numbers
    no set has (inf, times past 1e5 beats, keys / velocities out of MIDI's
    range) are skipped; at most a million notes per track;
  - lyric import: `@` positions of too many digits and endless lines no
    longer crash;
  - chord detector: the grid always ends (slice length 0, a far-out song end);
  - transport hand-off: a reader never returns fields of two blocks (it
    waits for the writer; sequentially consistent atomics);
  - state set off the message thread is handed back until applied, and
    never applied over a newer one.

- **v0.1.0** (first version, [PR #1](https://github.com/joris-klingen/boothcue/pull/1)):
  - core: `.als` parser, song model, lyric import (plain C++);
  - plugin scaffold, singer display, linking and watching the set, editor,
    tap-along, everything saved with the set;
  - lyric import for real pasted text (Bovenburen test cases);
  - chord detector and chords from the set's MIDI; display with three zoom
    levels and Live's clip colours; notes below A0 ignored;
  - BoothCue's own chords and lyrics database (chart and timed-lyrics text,
    chords as MIDI to drag into Live);
  - CI on Linux only (plugin with pluginval, core with the ALSmuse
    cross-check);
  - docs, screenshots and the Mac installer.
