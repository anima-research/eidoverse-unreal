#!/bin/sh
# Eidoverse.LiveChat end to end, headless: a scratch sequencer from the eidoverse-worlds checkout on loopback,
# the two bots of chat_live_bot.ts, and the real native bridge in the editor's automation runner.
# Never touches a production world; scratch state goes to a fresh temp directory, printed at the end.
#
#   sh Tools/eidoverse/run_chat_live_test.sh
#
# EIDOVERSE_REPO  the server checkout (default ~/connectome-local/eidoverse-worlds-native, unreal/native-client)
# UE_ROOT         the engine (default /Users/Shared/Epic Games/UE_5.8)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORLDS=${EIDOVERSE_REPO:-$HOME/connectome-local/eidoverse-worlds-native}
UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
PORT=${PORT:-19041}; CPORT=${CPORT:-19042}; LOUD=${LOUD:-80}
WORLD="chat-live-$(date +%s)"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-chat-live.XXXXXX")
mkdir -p "$SCRATCH/worlds"
[ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer

cleanup() { kill "${BOT:-}" "${SERVER:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM

# a fast verb rate so seeding takes seconds; loopback only
(cd "$WORLDS" && HOST=127.0.0.1 PORT="$PORT" VERB_RATE=400 SKIP_OPT_SWEEP=1 WORLDS_DIR="$SCRATCH/worlds" \
  exec bun server/server.ts >"$SCRATCH/server.log" 2>&1) &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/worlds" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 100 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done

bun "$ROOT/Tools/eidoverse/chat_live_bot.ts" "ws://127.0.0.1:$PORT/ws" "$WORLD" "$CPORT" unrealtest "$LOUD" >"$SCRATCH/bot.log" 2>&1 &
BOT=$!
i=0; until curl -sf "http://127.0.0.1:$CPORT/ready" 2>/dev/null | grep -q '"seeded":true'; do i=$((i+1)); [ $i -lt 300 ] || { echo "bots never seeded; see $SCRATCH/bot.log"; exit 2; }; sleep 0.2; done

EIDO_LIVE_CHAT_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_CHAT_WORLD="$WORLD" EIDO_LIVE_CHAT_CONTROL="http://127.0.0.1:$CPORT" EIDO_LIVE_CHAT_LOUD="$LOUD" \
  "$UE/Engine/Binaries/Mac/UnrealEditor-Cmd" "$ROOT/Test1.uproject" -ExecCmds="Automation RunTests Eidoverse.LiveChat" \
  -TestExit="Automation Test Queue Empty" -unattended -nullrhi -nosplash -nosound -stdout -FullStdOutLogOutput -NoP4 >"$SCRATCH/editor.log" 2>&1 || true

grep -E "LogAutomationController: (Display|Error|Warning).*(LiveChat|scrollback|resync|window|chime|session|joined)|Test Completed\. Result=" "$SCRATCH/editor.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' || true
echo "scratch: $SCRATCH"
grep -q 'Result={Success} Name={LiveChat}' "$SCRATCH/editor.log"
