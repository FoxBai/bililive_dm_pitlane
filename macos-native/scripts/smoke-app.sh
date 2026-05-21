#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="${APP_DIR:-$ROOT_DIR/build/PitlaneDanmaku.app}"
APP_BINARY="$APP_DIR/Contents/MacOS/PitlaneDanmakuMac"
PORT="${PITLANE_OBS_PORT:-17333}"
HEALTH_URL="http://127.0.0.1:$PORT/health"
OVERLAY_URL="http://127.0.0.1:$PORT/overlay"
CAR_URL="http://127.0.0.1:$PORT/cars/car_01.png"
KEEP_RUNNING="${PITLANE_SMOKE_KEEP_RUNNING:-0}"

if [[ "${PITLANE_SMOKE_BUILD:-1}" == "1" || ! -x "$APP_BINARY" ]]; then
  bash "$ROOT_DIR/scripts/build-app.sh" >/dev/null
fi

pkill -f "$APP_BINARY" 2>/dev/null || true
open "$APP_DIR"

pid=""
for _ in $(seq 1 40); do
  pid="$(pgrep -f "$APP_BINARY" | head -n 1 || true)"
  if [[ -n "$pid" ]]; then
    break
  fi
  sleep 0.25
done

if [[ -z "$pid" ]]; then
  echo "PitlaneDanmaku.app did not start." >&2
  exit 1
fi

cleanup() {
  if [[ "$KEEP_RUNNING" != "1" ]]; then
    kill "$pid" 2>/dev/null || true
  fi
}
trap cleanup EXIT

health=""
for _ in $(seq 1 60); do
  if health="$(curl -fsS "$HEALTH_URL" 2>/dev/null)"; then
    break
  fi
  sleep 0.25
done

if [[ "$health" != *'"status":"ok"'* ]]; then
  echo "Health check failed: $health" >&2
  exit 1
fi

curl -fsS -I "$OVERLAY_URL" >/dev/null
curl -fsS -I "$CAR_URL" >/dev/null

swift -e '
import CoreGraphics
import Darwin
import Foundation

let pid = Int32(CommandLine.arguments[1])!
let infos = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID)! as NSArray
var bestArea = 0
var bestDescription = "missing"

for case let info as NSDictionary in infos {
    guard (info[kCGWindowOwnerPID as String] as? Int32) == pid,
          (info[kCGWindowLayer as String] as? Int) == 0,
          let bounds = info[kCGWindowBounds as String] as? NSDictionary else {
        continue
    }

    let width = bounds["Width"] as? Int ?? 0
    let height = bounds["Height"] as? Int ?? 0
    let area = width * height
    if area > bestArea {
        bestArea = area
        bestDescription = "\(width)x\(height)"
    }
}

guard bestArea >= 600_000 else {
    fputs("Main window not visible; best window: \(bestDescription)\n", stderr)
    exit(1)
}

print("Main window visible: \(bestDescription)")
' "$pid"

echo "Smoke OK: $HEALTH_URL"
