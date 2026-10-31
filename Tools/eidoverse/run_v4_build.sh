#!/bin/sh
# Eidoverse.LiveV4Build and Eidoverse.LiveBuildKeys (T11, slice C) through the local bridge: a scratch current-protocol server
# (EIDOVERSE_REPO, default the Bridge's engine/ submodule) with two founded worlds, each owned by the name its test joins as
# (the first seeded by v4_seed.ts); this client in a game session (offscreen). Loopback only; the scratch dir is printed at the end.
#
#   sh Tools/eidoverse/run_v4_build.sh           (UE_ROOT, UE_ARGS as run_v4_join.sh; V4_TESTS: which to run, default both)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ENGINE=${EIDOVERSE_REPO:-$ROOT/Plugins/Eidoverse/Bridge/engine}
PORT=${PORT:-19063}
STAMP=$(date +%s)
WORLD="v4-build-$STAMP"; KEYS="v4-keys-$STAMP"; OWNER=v4builder
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/eido-v4-build.XXXXXX")
mkdir -p "$SCRATCH/worlds" "$SCRATCH/opt"
cleanup() { kill "${SERVER:-}" 2>/dev/null || true; }
trap cleanup EXIT INT TERM
SENV="HOST=127.0.0.1 PORT=$PORT JOIN_TOKEN= SERVER_OPERATORS= TRUST_NAMES=1 SKIP_OPT_SWEEP=1 RECORD_FRAMES=0 BUN_RUNTIME_TRANSPILER_CACHE_PATH=0
  WORLDS_DIR=$SCRATCH/worlds OPT_DIR=$SCRATCH/opt RELAY_STATE_DIR=$SCRATCH/opt SESSIONS_PATH=$SCRATCH/sessions.json"
for W in "$WORLD:$OWNER" "$KEYS:buildkeys"; do
  # shellcheck disable=SC2086
  (cd "$ENGINE" && env $SENV bun tools/protocol/world.ts found "${W%%:*}" --owner "${W#*:}") >>"$SCRATCH/found.log" 2>&1 || { echo "found failed; see $SCRATCH/found.log"; exit 2; }
done
# shellcheck disable=SC2086
(cd "$ENGINE" && exec env $SENV bun server/server.ts) >"$SCRATCH/server.log" 2>&1 &
SERVER=$!
i=0; until curl -sf "http://127.0.0.1:$PORT/version" >/dev/null 2>&1; do i=$((i+1)); [ $i -lt 150 ] || { echo "server never came up; see $SCRATCH/server.log"; exit 2; }; sleep 0.2; done
bun "$ROOT/Tools/eidoverse/v4_seed.ts" "ws://127.0.0.1:$PORT/ws" "$WORLD" "$OWNER" >"$SCRATCH/seed.log" 2>&1 || { echo "seed failed; see $SCRATCH/seed.log"; exit 2; }
export EIDO_LIVE_V4_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_V4_WORLD="$WORLD" EIDO_LIVE_V4_OWNER="$OWNER" EIDO_LIVE_BUILD_URL="ws://127.0.0.1:$PORT/ws" EIDO_LIVE_BUILD_WORLD="$KEYS" EIDOVERSE_BRIDGE=1
TESTS=${V4_TESTS:-Eidoverse.LiveV4Build+Eidoverse.LiveBuildKeys}
if [ -n "${UE_EDITOR:-}" ]; then EDITOR=$UE_EDITOR; PROJECT="$ROOT/Test1.uproject"
elif [ -n "${WSL_DISTRO_NAME:-}" ]; then
  UE=${UE_ROOT:-"/mnt/c/Program Files/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Win64/UnrealEditor.exe"; PROJECT=$(wslpath -w "$ROOT/Test1.uproject")
  export WSLENV="EIDO_LIVE_V4_URL:EIDO_LIVE_V4_WORLD:EIDO_LIVE_V4_OWNER:EIDO_LIVE_BUILD_URL:EIDO_LIVE_BUILD_WORLD:EIDOVERSE_BRIDGE${WSLENV:+:$WSLENV}"
else
  UE=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
  EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"; PROJECT="$ROOT/Test1.uproject"
  [ -n "${DEVELOPER_DIR:-}" ] || export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
"$EDITOR" "$PROJECT" -game -RenderOffscreen -ResX=1280 -ResY=720 \
  -ExecCmds="Automation RunTests $TESTS" -TestExit="Automation Test Queue Empty" \
  -unattended -nosound -nosplash -stdout -FullStdOutLogOutput -NoP4 ${UE_ARGS:-} >"$SCRATCH/game.log" 2>&1 || true
echo "--- client"; grep -aE "EIDOVERSE (bridge/error|joined)|EIDOVERSE_V4_BUILD|couldn't join|Test Completed\. Result=|Eidoverse.Live(V4Build|BuildKeys)|(ok  |FAIL) " "$SCRATCH/game.log" | sed -E 's/^\[[^]]*\]\[[^]]*\]//' | tr -d '\r' || true
echo "scratch: $SCRATCH"
! grep -q 'Result={Fail}' "$SCRATCH/game.log" && [ "$(grep -c 'Result={Success}' "$SCRATCH/game.log")" -ge "$(echo "$TESTS" | tr '+' '\n' | wc -l)" ]
