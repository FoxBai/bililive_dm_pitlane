#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
APP_DIR="$ROOT_DIR/build/PitlaneDanmaku.app"
APP_NAME="PitlaneDanmaku"
VERSION="${PITLANE_VERSION:-$(tr -d '[:space:]' < "$ROOT_DIR/VERSION")}"
VOLUME_NAME="${VOLUME_NAME:-Pitlane Danmaku}"
DMG_PATH="${DMG_PATH:-$DIST_DIR/$APP_NAME-$VERSION-macOS.dmg}"
SIGN_IDENTITY="${CODE_SIGN_IDENTITY:--}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
SKIP_CODESIGN="${SKIP_CODESIGN:-0}"
BACKGROUND_PATH="$ROOT_DIR/build/dmg-background.png"
README_PATH="$ROOT_DIR/Resources/README.txt"

bash "$ROOT_DIR/scripts/build-app.sh" >/dev/null
mkdir -p "$DIST_DIR"
swift "$ROOT_DIR/scripts/render-dmg-background.swift" "$BACKGROUND_PATH" "$VERSION"

if [[ "$SKIP_CODESIGN" != "1" ]]; then
  if [[ "$SIGN_IDENTITY" == "-" ]]; then
    codesign --force --deep --sign - "$APP_DIR"
  else
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP_DIR"
  fi
  codesign --verify --deep --strict --verbose=2 "$APP_DIR"
fi

stage_dir="$(mktemp -d "$ROOT_DIR/build/dmg-stage.XXXXXX")"
mount_dir="$(mktemp -d "$ROOT_DIR/build/dmg-mount.XXXXXX")"
rw_dmg="$ROOT_DIR/build/$APP_NAME-$VERSION-rw.dmg"
device=""
cleanup() {
  if [[ -n "$device" ]]; then
    hdiutil detach "$device" >/dev/null 2>&1 || true
  fi
  rm -rf "$stage_dir"
  rm -rf "$mount_dir"
  rm -f "$rw_dmg"
}
trap cleanup EXIT

mkdir -p "$stage_dir/.background"
cp -R "$APP_DIR" "$stage_dir/"
cp "$BACKGROUND_PATH" "$stage_dir/.background/background.png"
cp "$README_PATH" "$stage_dir/使用说明.txt"
ln -s /Applications "$stage_dir/Applications"

rm -f "$DMG_PATH"
rm -f "$rw_dmg"
hdiutil create \
  -volname "$VOLUME_NAME" \
  -srcfolder "$stage_dir" \
  -ov \
  -format UDRW \
  "$rw_dmg" >/dev/null

attach_output="$(hdiutil attach "$rw_dmg" -readwrite -noverify -noautoopen -mountpoint "$mount_dir")"
device="$(printf '%s\n' "$attach_output" | awk '/^\/dev\// {print $1; exit}')"

if [[ -z "$device" ]]; then
  echo "Unable to mount temporary DMG." >&2
  exit 1
fi

if [[ "${DMG_SKIP_FINDER_LAYOUT:-0}" != "1" && "${CI:-}" != "true" ]]; then
  osascript >/dev/null <<APPLESCRIPT || echo "Warning: Finder DMG layout was skipped." >&2
set dmgFolder to POSIX file "$mount_dir" as alias
tell application "Finder"
  open dmgFolder
  set current view of container window of dmgFolder to icon view
  set toolbar visible of container window of dmgFolder to false
  set statusbar visible of container window of dmgFolder to false
  set bounds of container window of dmgFolder to {120, 120, 760, 540}
  set theViewOptions to the icon view options of container window of dmgFolder
  set arrangement of theViewOptions to not arranged
  set icon size of theViewOptions to 96
  set background picture of theViewOptions to file ".background:background.png" of dmgFolder
  set position of item "$APP_NAME.app" of dmgFolder to {170, 225}
  set position of item "Applications" of dmgFolder to {470, 225}
  set position of item "使用说明.txt" of dmgFolder to {320, 340}
  update dmgFolder without registering applications
  delay 1
  close container window of dmgFolder
  end tell
APPLESCRIPT
fi

hdiutil detach "$device" >/dev/null
hdiutil convert "$rw_dmg" -format UDZO -o "$DMG_PATH" >/dev/null

if [[ "$SKIP_CODESIGN" != "1" && "$SIGN_IDENTITY" != "-" ]]; then
  codesign --force --sign "$SIGN_IDENTITY" "$DMG_PATH"
fi

if [[ -n "$NOTARY_PROFILE" ]]; then
  if [[ "$SIGN_IDENTITY" == "-" || "$SKIP_CODESIGN" == "1" ]]; then
    echo "NOTARY_PROFILE requires CODE_SIGN_IDENTITY and codesigning enabled." >&2
    exit 1
  fi

  xcrun notarytool submit "$DMG_PATH" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$DMG_PATH"
fi

echo "$DMG_PATH"
