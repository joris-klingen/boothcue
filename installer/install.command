#!/bin/zsh
# BoothCue installer: double-click to install the VST3 and the standalone app on this Mac.
# Copies the bundles that sit next to this script, then strips the quarantine flag so Gatekeeper
# doesn't block the (ad-hoc signed, not notarised) binaries.
#
# First time: macOS may refuse to open this script because it was downloaded. Right-click it,
# choose Open, and confirm.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -d "$HERE/BoothCue.vst3" ]; then
  echo "ERROR: BoothCue.vst3 not found next to this script ($HERE)" >&2
  exit 1
fi

VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DEST="/Applications"
[ -w "$APP_DEST" ] || APP_DEST="$HOME/Applications"
mkdir -p "$VST3_DEST" "$APP_DEST"

echo "Installing BoothCue from: $HERE"

rm -rf "$VST3_DEST/BoothCue.vst3"
cp -R "$HERE/BoothCue.vst3" "$VST3_DEST/"
xattr -dr com.apple.quarantine "$VST3_DEST/BoothCue.vst3" 2>/dev/null || true
echo "  VST3       -> $VST3_DEST/BoothCue.vst3"

if [ -d "$HERE/BoothCue.app" ]; then
  rm -rf "$APP_DEST/BoothCue.app"
  cp -R "$HERE/BoothCue.app" "$APP_DEST/"
  xattr -dr com.apple.quarantine "$APP_DEST/BoothCue.app" 2>/dev/null || true
  echo "  Standalone -> $APP_DEST/BoothCue.app"
fi

echo "Done. Rescan plug-ins in Live (Settings > Plug-Ins) if it was running."
