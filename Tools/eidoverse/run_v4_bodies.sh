#!/bin/sh
# Eidoverse.LiveV4Bodies (slice D), end to end: run_v4_join.sh's scratch current-protocol server, with v4_bodies_bot.ts
# holding the world (a bench with a seat, a grabbable lamp, a guest; it makes the client owner); this client joins through
# the local bridge and works the access tab, holding and the seat. Loopback only; scratch printed at the end.
#
#   sh Tools/eidoverse/run_v4_bodies.sh          (UE_ROOT, UE_ARGS, EIDOVERSE_REPO as run_v4_join.sh)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=${EIDOVERSE_REPO:-$ROOT/Plugins/Eidoverse/Bridge/engine}
PORT=${PORT:-19064}
STAMP=$(date +%s)
WORLD="v4-bodies-$STAMP"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-v4-bodies.XXXXXX")
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
bun "$ROOT/Tools/eidoverse/v4_bodies_bot.ts" "ws://127.0.0.1:$PORT/ws" "$WORLD" founder unrealbodies guestbot >"$SCRATCH/seed.log" 2>"$SCRATCH/bot.err" &
BOT=$!
i=0; until grep -q '"bench"' "$SCRATCH/seed.log" 2>/dev/null; do i=$((i+1)); [ $i -lt 150 ] || { echo "seed failed; see $SCRATCH/seed.log, bot.err"; exit 2; }; sleep 0.2; done
grep -q '"ok":true' "$SCRATCH/seed.log" || { echo "seed refused; see $SCRATCH/seed.log"; exit 2; }
field() { sed -n "s/.*\"$1\":\"\([^\"]*\)\".*/\1/p" "$SCRATCH/seed.log" | head -1; }
export EIDO_LIVE_V4_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_V4_WORLD="$WORLD" EIDO_LIVE_V4_BENCH=$(field bench) EIDO_LIVE_V4_LAMP=$(field lamp) EIDO_LIVE_V4_GUEST=guestbot
WIN=""; if [ -n "${WSL_DISTRO_NAME:-}" ]; then WIN=1; fi
if [ -n "$WIN" ]; then
  UE=${UE_ROOT:-"/mnt/c/Program Files/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Win64/UnrealEditor.exe"; PROJECT=$(wslpath -w "$ROOT/Test1.uproject")
  export WSLENV="EIDO_LIVE_V4_URL:EIDO_LIVE_V4_WORLD:EIDO_LIVE_V4_BENCH:EIDO_LIVE_V4_LAMP:EIDO_LIVE_V4_GUEST:EIDOVERSE_BRIDGE${WSLENV:+:$WSLENV}"
else
  UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"; PROJECT="$ROOT/Test1.uproject"
  [ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
timeout "${EIDO_RUN_TIMEOUT:-900}" "$EDITOR" "$PROJECT" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests Eidoverse.LiveV4Bodies" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 ${UE_ARGS:-} >"$SCRATCH/game.log" 2>&1 || true
echo "--- server"; grep -a "\[world:$WORLD\]" "$SCRATCH/server.log" | tr -d '\r' || true
echo "--- bot"; cat "$SCRATCH/seed.log"
echo "--- client"; grep -aE "EIDOVERSE (bridge|joined)|EIDOVERSE_V4_BODIES|couldn't join|Test Completed\. Result=|Eidoverse.LiveV4Bodies" "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tr -d '\r' || true
echo "scratch: $SCRATCH"
! grep -q 'Result={Fail}' "$SCRATCH/game.log" && grep -q 'Result={Success}' "$SCRATCH/game.log"
