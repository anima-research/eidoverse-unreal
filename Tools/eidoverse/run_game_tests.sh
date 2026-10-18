#!/bin/sh
# The game-session live checks, in one game session rendered offscreen, against a scratch sequencer on loopback
# (EIDOVERSE_REPO, default ~/connectome-local/eidoverse-worlds-native):
#   Eidoverse.LiveBuildKeys  build mode's keys and picks through the real input path
#   Eidoverse.LiveAccess     world access: the access tab, the door, timed grants, 4010 at a closed door and on
#                            being removed (with Tools/eidoverse/access_bot.ts, signed in via a scratch issuer)
# Never touches a production world; scratch state goes to a fresh temp directory, printed at the end.
#
#   sh Tools/eidoverse/run_game_tests.sh [Eidoverse.LiveBuildKeys|Eidoverse.LiveAccess]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORLDS=${EIDOVERSE_REPO:-$HOME/connectome-local/eidoverse-worlds-native}
UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
PORT=${PORT:-19043}; CPORT=${CPORT:-19045}
STAMP=$(date +%s)
WORLD="build-keys-$STAMP"
TESTS=${1:-Eidoverse.LiveBuildKeys+Eidoverse.LiveAccess}
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-build-keys.XXXXXX")
mkdir -p "$SCRATCH/worlds"
[ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
cleanup() { kill "${SERVER:-}" "${BOT:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
# a scratch identity issuer, so the access bot's keeper can be a signed-in owner (closing a world takes one)
ISSUER=$(bun "$ROOT/Tools/eidoverse/access_bot.ts" keygen "$SCRATCH/keys")
mkdir -p "$SCRATCH/opt"
(cd "$WORLDS" && HOST=127.0.0.1 PORT="$PORT" SKIP_OPT_SWEEP=1 RECORD_FRAMES=0 WORLDS_DIR="$SCRATCH/worlds" OPT_DIR="$SCRATCH/opt" RELAY_STATE_DIR="$SCRATCH/opt" \
  HN_SESSIONS_FILE="$SCRATCH/sessions.json" AGENT_TOKENS_PATH="$SCRATCH/tokens.json" HN_ISSUER_KEY="$ISSUER" HN_ISS=id.test HN_AUD=eidoverse \
  HN_NATIVE_ORIGIN="http://127.0.0.1:$PORT" HN_REQUIRE_LOGIN=0 exec bun server/server.ts >"$SCRATCH/server.log" 2>&1) &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/worlds" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 100 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done
bun "$ROOT/Tools/eidoverse/access_bot.ts" "ws://127.0.0.1:$PORT/ws" "access-$STAMP" "closed-$STAMP" "kept-$STAMP" "$CPORT" "$SCRATCH/keys" >"$SCRATCH/bot.log" 2>&1 &
BOT=$!
i=0; until curl -sf "http://127.0.0.1:$CPORT/report" 2>/dev/null | grep '"keeperClosed":"none"' | grep -q '"keeperOpen":true'; do i=$((i+1)); [ $i -lt 100 ] || { echo "the keeper never closed its world; see $SCRATCH/bot.log"; exit 2; }; sleep 0.2; done
EIDO_LIVE_BUILD_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_BUILD_WORLD="$WORLD" \
  EIDO_LIVE_ACCESS_WORLD="access-$STAMP" EIDO_LIVE_ACCESS_CLOSED="closed-$STAMP" EIDO_LIVE_ACCESS_KEPT="kept-$STAMP" EIDO_LIVE_ACCESS_CONTROL="http://127.0.0.1:$CPORT" \
  "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor" "$ROOT/Test1.uproject" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests $TESTS" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 >"$SCRATCH/game.log" 2>&1 || true
sed -n '/BeginEvents: Eidoverse.Live/,/EndEvents: Eidoverse.Live/p' "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | grep -v "LogStreaming\|EIDOVERSE_HITCH\|LogViewport\|\[log\]" || true
grep -E "Test Completed\. Result=" "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' || true
echo "scratch: $SCRATCH"
! grep -q 'Result={Fail}' "$SCRATCH/game.log" && grep -q 'Result={Success}' "$SCRATCH/game.log"
