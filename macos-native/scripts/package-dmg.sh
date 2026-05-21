#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist}"
APP_DIR="$ROOT_DIR/build/PitlaneDanmaku.app"
APP_NAME="PitlaneDanmaku"
VERSION="${PITLANE_VERSION:-0.1.0}"
VOLUME_NAME="${VOLUME_NAME:-Pitlane Danmaku}"
DMG_PATH="${DMG_PATH:-$DIST_DIR/$APP_NAME-$VERSION-macOS.dmg}"
SIGN_IDENTITY="${CODE_SIGN_IDENTITY:--}"
NOTARY_PROFILE="${NOTARY_PROFILE:-}"
SKIP_CODESIGN="${SKIP_CODESIGN:-0}"

bash "$ROOT_DIR/scripts/build-app.sh" >/dev/null
mkdir -p "$DIST_DIR"

if [[ "$SKIP_CODESIGN" != "1" ]]; then
  if [[ "$SIGN_IDENTITY" == "-" ]]; then
    codesign --force --deep --sign - "$APP_DIR"
  else
    codesign --force --deep --options runtime --timestamp --sign "$SIGN_IDENTITY" "$APP_DIR"
  fi
  codesign --verify --deep --strict --verbose=2 "$APP_DIR"
fi

stage_dir="$(mktemp -d "$ROOT_DIR/build/dmg-stage.XXXXXX")"
cleanup() {
  rm -rf "$stage_dir"
}
trap cleanup EXIT

cp -R "$APP_DIR" "$stage_dir/"
ln -s /Applications "$stage_dir/Applications"

rm -f "$DMG_PATH"
hdiutil create \
  -volname "$VOLUME_NAME" \
  -srcfolder "$stage_dir" \
  -ov \
  -format UDZO \
  "$DMG_PATH" >/dev/null

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
