#!/bin/sh
# Eidoverse.LiveV4Join, end to end: a scratch current-protocol server (EIDOVERSE_REPO, default the Bridge's engine/ submodule)
# with one founded, seeded world; this client in a game session (offscreen) joins it, first on its own (refused, 4005), then
# through the local bridge (Plugins/Eidoverse/Bridge): snapshot, bridge/ready, the scene. Loopback only; scratch printed at the end.
#
#   sh Tools/eidoverse/run_v4_join.sh            (UE_ROOT: the engine install; on WSL, the Windows one under /mnt/c;
#                                                 UE_ARGS: more editor arguments; EIDO_LIVE_V4_SHOT=0: no screenshot)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=${EIDOVERSE_REPO:-$ROOT/Plugins/Eidoverse/Bridge/engine}
PORT=${PORT:-19061}
STAMP=$(date +%s)
WORLD="v4-join-$STAMP"
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-v4-join.XXXXXX")
mkdir -p "$SCRATCH/worlds" "$SCRATCH/opt"
cleanup() { kill "${SERVER:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
SENV="HOST=127.0.0.1 PORT=$PORT JOIN_TOKEN= SERVER_OPERATORS= TRUST_NAMES=1 SKIP_OPT_SWEEP=1 RECORD_FRAMES=0 BUN_RUNTIME_TRANSPILER_CACHE_PATH=0
  WORLDS_DIR=$SCRATCH/worlds OPT_DIR=$SCRATCH/opt RELAY_STATE_DIR=$SCRATCH/opt SESSIONS_PATH=$SCRATCH/sessions.json"
# shellcheck disable=SC2086
(cd "$ENGINE" && env $SENV bun tools/protocol/world.ts found "$WORLD" --owner founder) >"$SCRATCH/found.log" 2>&1 || { echo "found failed; see $SCRATCH/found.log"; exit 2; }
# shellcheck disable=SC2086
(cd "$ENGINE" && exec env $SENV bun server/server.ts) >"$SCRATCH/server.log" 2>&1 &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/version" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 150 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done
bun "$ROOT/Tools/eidoverse/v4_seed.ts" "ws://127.0.0.1:$PORT/ws" "$WORLD" founder >"$SCRATCH/seed.log" 2>&1 || { echo "seed failed; see $SCRATCH/seed.log"; exit 2; }
export EIDO_LIVE_V4_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_V4_WORLD="$WORLD" EIDO_LIVE_V4_ITEMS=2
WIN=""; if [ -n "${WSL_DISTRO_NAME:-}" ]; then WIN=1; fi
if [ -n "$WIN" ]; then
  UE=${UE_ROOT:-"/mnt/c/Program Files/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Win64/UnrealEditor.exe"; PROJECT=$(wslpath -w "$ROOT/Test1.uproject")
  export WSLENV="EIDO_LIVE_V4_URL:EIDO_LIVE_V4_WORLD:EIDO_LIVE_V4_ITEMS:EIDO_LIVE_V4_SHOT:EIDOVERSE_BRIDGE${WSLENV:+:$WSLENV}"
else
  UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"; PROJECT="$ROOT/Test1.uproject"
  [ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
"$EDITOR" "$PROJECT" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests Eidoverse.LiveV4Join" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 ${UE_ARGS:-} >"$SCRATCH/game.log" 2>&1 || true
SHOT=$(find "$ROOT/Saved/Screenshots" -name 'eidoverse-v4-join*.png' -newer "$SCRATCH/seed.log" 2>/dev/null | head -1)
[ -z "$SHOT" ] || cp "$SHOT" "$SCRATCH/"
echo "--- server"; grep -a "\[world:$WORLD\]" "$SCRATCH/server.log" | tr -d '\r' || true
echo "--- client"; grep -aE "EIDOVERSE (bridge|joined)|EIDOVERSE_V4|couldn't join|Test Completed\. Result=|Eidoverse.LiveV4Join" "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tr -d '\r' || true
echo "scratch: $SCRATCH${SHOT:+ (screenshot: $(basename "$SHOT"))}"
! grep -q 'Result={Fail}' "$SCRATCH/game.log" && grep -q 'Result={Success}' "$SCRATCH/game.log"
