#!/bin/sh
# Eidoverse.LiveV4Chat, end to end (slice B, Plugins/Eidoverse/Bridge/SLICES.md): a scratch current-protocol server with one
# founded world, its founder as v4_chat_bot.ts, and this client in a game session (offscreen) joined through the local bridge:
# chat, narration, rights, pose.anim, capture, /defs, /worlds. Loopback only; scratch printed at the end.
#
#   sh Tools/eidoverse/run_v4_chat.sh            (as run_v4_join.sh: UE_ROOT, UE_ARGS, EIDOVERSE_REPO)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=${EIDOVERSE_REPO:-$ROOT/Plugins/Eidoverse/Bridge/engine}
PORT=${PORT:-19071}; CPORT=${CPORT:-19072}
WORLD="v4-chat-$(date +%s)"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-v4-chat.XXXXXX")
mkdir -p "$SCRATCH/worlds" "$SCRATCH/opt"
cleanup() { kill "${BOT:-}" "${SERVER:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
SENV="HOST=127.0.0.1 PORT=$PORT JOIN_TOKEN= SERVER_OPERATORS= TRUST_NAMES=1 SKIP_OPT_SWEEP=1 RECORD_FRAMES=0 BUN_RUNTIME_TRANSPILER_CACHE_PATH=0
  WORLDS_DIR=$SCRATCH/worlds OPT_DIR=$SCRATCH/opt RELAY_STATE_DIR=$SCRATCH/opt SESSIONS_PATH=$SCRATCH/sessions.json"
# shellcheck disable=SC2086
(cd "$ENGINE" && env $SENV bun tools/protocol/world.ts found "$WORLD" --owner founder) >"$SCRATCH/found.log" 2>&1 || { echo "found failed; see $SCRATCH/found.log"; exit 2; }
# shellcheck disable=SC2086
(cd "$ENGINE" && exec env $SENV bun server/server.ts) >"$SCRATCH/server.log" 2>&1 &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/version" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 150 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done
bun "$ROOT/Tools/eidoverse/v4_chat_bot.ts" "ws://127.0.0.1:$PORT/ws" "$WORLD" founder "$CPORT" unrealv4 >"$SCRATCH/bot.log" 2>&1 &
BOT=$!
i=0; until curl -sf "http://127.0.0.1:$CPORT/ready" 2>/dev/null | grep -q '"ready":true'; do i=$((i+1)); [ $i -lt 150 ] || { echo "the bot never got ready; see $SCRATCH/bot.log"; exit 2; }; sleep 0.2; done
export EIDO_LIVE_V4_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_V4_WORLD="$WORLD" EIDO_LIVE_V4_CONTROL="http://127.0.0.1:$CPORT"
if [ -n "${UE_EDITOR:-}" ]; then EDITOR=$UE_EDITOR; PROJECT="$ROOT/Test1.uproject"
elif [ -n "${WSL_DISTRO_NAME:-}" ]; then
  UE=${UE_ROOT:-"/mnt/c/Program Files/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Win64/UnrealEditor.exe"; PROJECT=$(wslpath -w "$ROOT/Test1.uproject")
  export WSLENV="EIDO_LIVE_V4_URL:EIDO_LIVE_V4_WORLD:EIDO_LIVE_V4_CONTROL:EIDOVERSE_BRIDGE${WSLENV:+:$WSLENV}"
else
  UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"; PROJECT="$ROOT/Test1.uproject"
  [ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
"$EDITOR" "$PROJECT" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests Eidoverse.LiveV4Chat" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 ${UE_ARGS:-} >"$SCRATCH/game.log" 2>&1 || true
echo "--- client"; grep -aE "EIDOVERSE (bridge|joined)|EIDOVERSE_V4CHAT|Error:|Test Completed\. Result=|Eidoverse.LiveV4Chat" "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tr -d '\r' || true
echo "scratch: $SCRATCH"
! grep -q 'Result={Fail}' "$SCRATCH/game.log" && grep -q 'Result={Success}' "$SCRATCH/game.log"
