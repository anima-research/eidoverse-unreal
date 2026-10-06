#!/bin/zsh
# Live tests that need fixtures or bots, each against a fresh world on the LOCAL preview
# server (port 8993; voice/TTS tests start their own SFU preview on 8994 and stop it).
# Never points at production. Usage: Tools/eidoverse/run_live_tests.sh [name ...]
#   names: parts loco body drag voice tts chat door residency flora seats-gizmos struct inspect md debug
# Prints each test's RESULT line. The editor must be closed (tests launch it headless).
set -u
ROOT="/Users/olena/Documents/Unreal Projects/Test1"
UE="/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
LOG="$HOME/Library/Logs/Unreal Engine/Test1Editor/Test1.log"
WORLDS="$HOME/connectome-local/eidoverse-worlds-native"
LIB="/Users/olena/unrealtest/eidoverse/commons-local.99SNUp/library"
TMP="${TMPDIR:-/tmp}/eido-live"; mkdir -p "$TMP"
M=eidoverse/assets/models
URL=ws://127.0.0.1:8993/ws

run_ue() {   # run_ue <script.py> <ENVVAR> <world> <RESULT tag>
  env "$2=$3" "$UE" "$ROOT/Test1.uproject" -RenderOffscreen -unattended -ExecutePythonScript="$ROOT/Tools/eidoverse/$1" >/dev/null 2>&1
  grep -a "$4" "$LOG" | tail -1 | sed 's/.*LogPython: Warning: //'
}
bot() { (cd "$WORLDS" && nohup bun "$ROOT/Tools/eidoverse/$1" "${@:2}" > "$TMP/$1.log" 2>&1 &) }
sfu_up() { (cd "$WORLDS" && VOICE_TRANSPORT=sfu SFU_ICE_SERVERS='[]' EIDOVERSE_DIR="$LIB" BUN_RUNTIME_TRANSPILER_CACHE_PATH=0 nohup bun tools/water/preview.ts /Users/olena/unrealtest/web-water/right-handed-bundle 8994 > "$TMP/sfu.log" 2>&1 & echo $! > "$TMP/sfu.pid"); sleep 4; }
sfu_down() { kill $(cat "$TMP/sfu.pid") 2>/dev/null; }
stop_bots() { pkill -f "Tools/eidoverse/.*_bot.ts" 2>/dev/null; true; }

t_parts() {
  local W=parts-$RANDOM
  local V="[{\"verb\":\"spawn\",\"args\":{\"id\":\"tank\",\"lib\":\"$M/scifi_cyberpunk_tank_military_artillery_apc_gun_weapon_animated.glb\",\"pos\":[0,0,0],\"yaw\":0}},{\"verb\":\"comp\",\"args\":{\"id\":\"tank\",\"type\":\"motion:turretbase_005\",\"data\":{\"type\":\"spin\",\"rpm\":10}}},{\"verb\":\"spawn\",\"args\":{\"id\":\"gate\",\"lib\":\"$M/scifi_perimeter_wall_gate.glb\",\"pos\":[-10,0,0],\"yaw\":0}},{\"verb\":\"comp\",\"args\":{\"id\":\"gate\",\"type\":\"picture\",\"data\":{\"src\":\"store/images/3306ef28ddbb9bc4.png\",\"part\":\"Cube.001\",\"lit\":\"self\",\"look\":\"a test pattern\"}}}]"
  (cd "$WORLDS" && POSE_BOT_ID=builder POSE_BOT_VERBS="$V" nohup bun "$ROOT/Tools/eidoverse/pose_bot.ts" $URL $W eidoverse/assets/vrms/shino.vrm 300 6 6 > "$TMP/parts.log" 2>&1 &); sleep 4
  run_ue test_parts.py PARTS_WORLD $W PARTS_RESULT; stop_bots
}
t_loco() {
  local W=loco-$RANDOM
  local V="[{\"verb\":\"spawn\",\"args\":{\"id\":\"ladderbox\",\"lib\":\"$M/crate_large_blue.glb\",\"pos\":[0,0,-2],\"yaw\":0}},{\"verb\":\"comp\",\"args\":{\"id\":\"ladderbox\",\"type\":\"traversal\",\"data\":{\"routes\":[{\"path\":[[0,0,1],[0,2.2,1],[0,2.2,0]],\"name\":\"ladder\",\"speed\":1.5}]}}},{\"verb\":\"spawn\",\"args\":{\"id\":\"boat\",\"lib\":\"$M/crate_large_red.glb\",\"pos\":[4,0,0],\"yaw\":0}},{\"verb\":\"comp\",\"args\":{\"id\":\"boat\",\"type\":\"vehicle\",\"data\":{\"helm\":[0,1,0],\"exit\":[0,0,-1.5],\"speed\":4}}},{\"verb\":\"spawn\",\"args\":{\"id\":\"step\",\"lib\":\"$M/crate_large_green.glb\",\"pos\":[-4,0,0],\"yaw\":0,\"scale\":0.7}}]"
  (cd "$WORLDS" && POSE_BOT_ID=builder POSE_BOT_VERBS="$V" nohup bun "$ROOT/Tools/eidoverse/pose_bot.ts" $URL $W eidoverse/assets/vrms/shino.vrm 200 8 8 > "$TMP/loco.log" 2>&1 &); sleep 4
  run_ue test_locomotion.py LOCO_WORLD $W LOCO_RESULT; stop_bots
}
t_body()  { local W=body-$RANDOM; bot drag_bot.ts $URL $W eidoverse/assets/vrms/shino.vrm unreal-body-test 200; run_ue test_body_physics.py BODY_WORLD $W BODY_RESULT; stop_bots; }
t_drag()  { local W=drg-$RANDOM; bot limp_bot.ts $URL $W eidoverse/assets/vrms/shino.vrm 100 0 2; run_ue test_drag_other.py DRAGGER_WORLD $W DRAGGER_RESULT; stop_bots; }
t_voice() { sfu_up; local W=voice-$RANDOM; bot voice_bot.ts ws://127.0.0.1:8994/ws $W voicebot 150 --tone=440 --pos=0,0,2; run_ue test_voice.py VOICE_WORLD $W VOICE_RESULT; stop_bots; sfu_down; }
t_tts()   { sfu_up; local W=tts-$RANDOM; bot tts_bot.ts 8927 150; bot voice_bot.ts ws://127.0.0.1:8994/ws $W voicebot 75 --no-tone; run_ue test_tts.py TTS_WORLD $W TTS_RESULT; stop_bots; sfu_down; }
t_chat()  { local W=chat-$RANDOM; bot talk_bot.ts $URL $W 3 unreal-chat-test 150; run_ue test_chat_tabs.py CHAT_WORLD $W CHAT_RESULT; stop_bots; }
t_door()      { run_ue test_door.py DOOR_WORLD door-$RANDOM DOOR_RESULT; }
t_residency() { run_ue test_residency.py RES_WORLD res-$RANDOM RESIDENCY_RESULT; }
t_flora()     { run_ue test_flora_push.py FLORA_WORLD flora-$RANDOM FLORA_RESULT; }
t_seats-gizmos() { run_ue test_seat_gizmos.py SG_WORLD sg-$RANDOM SEATGIZ_RESULT; }
t_struct()    { run_ue test_struct_preview.py SP_WORLD sp-$RANDOM STRUCTPREV_RESULT; }
t_inspect()   { run_ue test_inspect_fields.py INSPECT_WORLD insp-$RANDOM INSPECT_RESULT; }
t_md()        { run_ue test_chat_md.py MD_WORLD md-$RANDOM MD_RESULT; }
t_debug()     { run_ue test_debug_overlay.py DBG_WORLD dbg-$RANDOM DEBUG_RESULT; }

lsof -iTCP:8993 -sTCP:LISTEN >/dev/null 2>&1 || { echo "start the local preview on 8993 first (see README)"; exit 1; }
NAMES=(${@:-parts loco body drag voice tts chat door residency flora seats-gizmos struct inspect md debug})
for N in $NAMES; do printf "%-13s " "$N"; "t_$N"; done
