"""Nameplate anchoring, live (plateanchor.js): a standing bot's plate is rigid at its rest crown + gap over
the root; a lying bot's plate blends over its head (lie near 1) and sits far lower; a sitting bot's plate
comes down with the hips (live anchor, between). Bots: plate-stand (idle), plate-lie (lie), plate-sit (sit).
PLATE_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['PLATE_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'samples': []}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('PLATE_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 90: return done(False, 'timeout at ' + st['step'] + ' ' + json.dumps(result))
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs: return
    b = bs[0]; pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-plate'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        ids = ['plate-stand', 'plate-lie', 'plate-sit']
        got = {i: b.describe_peer_plate(i) for i in ids}
        if all(got.values()) and since > 10:
            st['samples'].append(got)
            if len(st['samples']) >= 30:
                result['first'] = st['samples'][0]; result['last'] = st['samples'][-1]
                offs = [float(s['plate-stand'].split('|')[2]) for s in st['samples']]
                result['stand_spread_cm'] = round(max(offs) - min(offs), 2)
                pc = unreal.GameplayStatics.get_player_controller(w, 0)
                pawn.set_actor_location(unreal.Vector(-150, 0, pawn.get_actor_location().z), False, True)
                pc.set_control_rotation(unreal.Rotator(0, -8, 0))
                return step('shot')
    if st['step'] == 'shot' and since > 1.5:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('end')
    if st['step'] == 'end' and since > 1.5:
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
