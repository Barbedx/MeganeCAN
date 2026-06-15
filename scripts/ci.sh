#!/usr/bin/env bash
# Local CI: native unit tests + both target builds. Run before committing.
#   bash scripts/ci.sh
# Native tests need a host g++ on PATH (portable MinGW / w64devkit works).
set -e
PIO="$HOME/.platformio/penv/Scripts/pio.exe"
REPO="$(cd "$(dirname "$0")/.." && pwd)"

echo "== native unit tests =="
"$PIO" test -e native -d "$REPO"
echo "== build esp32dev (bench) =="
"$PIO" run -e esp32dev -d "$REPO"
echo "== build esp32dev-mini (car) =="
"$PIO" run -e esp32dev-mini -d "$REPO"
echo "ALL GREEN"
