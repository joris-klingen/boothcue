# Lyric fixtures

Real lyrics (Bovenburen, from the band's "Bovenburen - Teksten" Google Doc), as
they come out of a copy-paste from Google Docs: plain text, headers without
brackets, Dutch section names, chord charts mixed in, pickups in parentheses,
repeat markers, choruses written out again without a header, non-breaking
spaces and curly quotes. `song-check` reads them (`lyricFixtureTests`) and
checks what the import makes of each.

| File | What it exercises |
|---|---|
| `papa-die-gaat-naar-de-club.txt` | title line, `verse 1` / `pre` / `chorus` headers, pickups `(Hij is)` |
| `de-langste-rij.txt` | chord charts, `Refrein:`, bare `Refrein` = repeat, `Brug: first line`, `x 2`, `Refrein x2` |
| `dertig-dagen.txt` | `verse` twice, chorus written out again without a header, `()` placeholder |
| `parijs.txt` | no headers at all (nothing can link), chord charts with `(Bb)` |
| `nedertown.txt` | slash chords, `(Csus4)`, a `(Roeren:)` chart line, no headers |
