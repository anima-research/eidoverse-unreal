#!/bin/sh
# Eidoverse.LiveBuildKeys: build mode's keys through the real input path, in a game session rendered offscreen,
# against a scratch sequencer on loopback (EIDOVERSE_REPO, default ~/connectome-local/eidoverse-worlds-native).
# Never touches a production world; scratch state goes to a fresh temp directory, printed at the end.
#
#   sh Tools/eidoverse/run_build_keys_test.sh
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORLDS=${EIDOVERSE_REPO:-$HOME/connectome-local/eidoverse-worlds-native}
UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
PORT=${PORT:-19043}
WORLD="build-keys-$(date +%s)"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-build-keys.XXXXXX")
mkdir -p "$SCRATCH/worlds"
[ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cleanup() { kill "${SERVER:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
(cd "$WORLDS" && HOST=127.0.0.1 PORT="$PORT" SKIP_OPT_SWEEP=1 WORLDS_DIR="$SCRATCH/worlds" exec bun server/server.ts >"$SCRATCH/server.log" 2>&1) &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/worlds" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 100 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done
EIDO_LIVE_BUILD_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_BUILD_WORLD="$WORLD" \
  "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/Test1.uproject" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests Eidoverse.LiveBuildKeys" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 >"$SCRATCH/game.log" 2>&1 || true
sed -n '/BeginEvents: Eidoverse.LiveBuildKeys/,/EndEvents: Eidoverse.LiveBuildKeys/p' "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' || true
grep -E "Test Completed\. Result=" "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' || true
echo "scratch: $SCRATCH"
grep -q 'Result={Success} Name={LiveBuildKeys}' "$SCRATCH/game.log"
