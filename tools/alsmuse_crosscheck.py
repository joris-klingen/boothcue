#!/usr/bin/env python3
"""Cross-check BoothCue's .als parser against ALSmuse (pip install alsmuse).

    python3 tools/alsmuse_crosscheck.py build/als-check song.als [more.als ...] [--track parts]

For every set, the sections printed by `als-check --dump` must equal the clips
ALSmuse reads from the same track. ALSmuse has no "2nd track" fallback, so sets
without a track of that name are reported as skipped.
"""
import subprocess
import sys
from pathlib import Path

from alsmuse.exceptions import TrackNotFoundError
from alsmuse.extractors import StructureTrackExtractor
from alsmuse.parser import parse_als_file

args = sys.argv[1:]
track = 'parts'
if '--track' in args:
    i = args.index('--track')
    track = args[i + 1]
    del args[i:i + 2]
if len(args) < 2:
    sys.exit(__doc__)
als_check, sets = args[0], args[1:]

bad = 0
for path in sets:
    try:
        theirs = [(s.start_beats, s.end_beats, s.name)
                  for s in StructureTrackExtractor(track).extract(parse_als_file(Path(path)))]
    except TrackNotFoundError:
        print(f'SKIP  {path}: no track named "{track}"')
        continue
    out = subprocess.run([als_check, '--dump', path, track], capture_output=True, text=True)
    if out.returncode != 0:
        print(f'FAIL  {path}: als-check: {out.stderr.strip()}')
        bad += 1
        continue
    ours = []
    for line in out.stdout.splitlines():
        start, end, name = line.split('\t', 2)
        ours.append((float(start), float(end), name))
    # ALSmuse keeps zero-length clips, BoothCue drops them.
    theirs = [t for t in theirs if t[1] > t[0]]
    same = len(ours) == len(theirs) and all(
        abs(a[0] - b[0]) < 1e-6 and abs(a[1] - b[1]) < 1e-6 and a[2] == b[2] for a, b in zip(ours, theirs))
    print(f'{"OK   " if same else "FAIL "} {path}: {len(ours)} sections')
    if not same:
        bad += 1
        print('  boothcue:', ours)
        print('  alsmuse: ', theirs)
sys.exit(1 if bad else 0)
