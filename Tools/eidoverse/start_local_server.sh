#!/bin/sh
set -eu
eido_source=${EIDOVERSE_SOURCE:-/Users/olena/connectome-local/eidoverse-worlds}
eido_scratch=$(mktemp -d /Users/olena/unrealtest/eidoverse-dev.XXXXXX)
mkdir -p "$eido_scratch/opt"
echo "Isolated Eidoverse: ws://127.0.0.1:8993/ws; data: $eido_scratch"
cd "$eido_source"
exec env HOST=127.0.0.1 PORT=8993 JOIN_TOKEN= WORLDS_DIR="$eido_scratch/worlds" OPT_DIR="$eido_scratch/opt" RELAY_STATE_DIR="$eido_scratch/opt" SKIP_OPT_SWEEP=1 RECORD_FRAMES=0 bun server/server.ts
