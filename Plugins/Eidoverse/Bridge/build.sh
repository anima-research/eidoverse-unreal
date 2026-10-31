#!/bin/sh
# Build the bridge as one executable per platform into Plugins/Eidoverse/Binaries/<Platform>/Bridge/ (EidoverseClient.Build.cs stages it).
#   sh Plugins/Eidoverse/Bridge/build.sh [mac-arm64|mac-x64|win64|linux]   (default: this machine's)
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
case "${1:-}" in
  "") case "$(uname -s)-$(uname -m)" in Darwin-arm64) T=mac-arm64;; Darwin-*) T=mac-x64;; *) T=linux;; esac;;
  *) T=$1;;
esac
case "$T" in
  mac-arm64) TARGET=bun-darwin-arm64; OUT=Mac/Bridge/eido-bridge;;
  mac-x64)   TARGET=bun-darwin-x64;   OUT=Mac/Bridge/eido-bridge;;
  win64)     TARGET=bun-windows-x64;  OUT=Win64/Bridge/eido-bridge.exe;;
  linux)     TARGET=bun-linux-x64;    OUT=Linux/Bridge/eido-bridge;;
  *) echo "usage: build.sh [mac-arm64|mac-x64|win64|linux]"; exit 2;;
esac
[ -f "$DIR/engine/shared/protocol/replica.js" ] || { echo "engine/ is empty: git submodule update --init Plugins/Eidoverse/Bridge/engine"; exit 2; }
mkdir -p "$DIR/../Binaries/$(dirname "$OUT")"
bun build --compile --target="$TARGET" "$DIR/src/main.ts" --outfile "$DIR/../Binaries/$OUT"
echo "built $(cd "$DIR/../Binaries" && pwd)/$OUT ($TARGET). On macOS sign and notarize it with the app (bun's JIT needs the allow-jit entitlement)."
