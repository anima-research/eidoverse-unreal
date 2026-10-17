"""This body, live: a bot streams scale 1.5, plateY 0.3, hear false; its body must be 1.5x and its
plate show the not-hearing mark. My size 1.4 / lift 0.2 resizes my capsule (feet kept) and rides
my pose (the bot logs it). Restores my prefs. BODYSIZE_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['BS_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    w = editor.get_game_world()
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge) if w else []
    if bs: bs[0].set_body_prefs(1, 0)
    unreal.log_warning('BODYSIZE_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 80: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-body'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and isinstance(pawn, unreal.EidoverseExplorer) and since > 10:
        result['peer_scale'] = b.get_peer_body_scale('size-bot'); result['peer_hear'] = b.get_peer_hear('size-bot')
        m = b.get_peer_body('size-bot'); result['peer_mesh_scale'] = round(m.get_editor_property('relative_scale3d').x, 2) if m else None
        st['feet0'] = pawn.get_actor_location().z - pawn.capsule_component.get_scaled_capsule_half_height()
        b.set_body_prefs(1.4, 0.2); return step('mine')
    if st['step'] == 'mine' and since > 2:
        cap = pawn.capsule_component
        result['my_half_cm'] = round(cap.get_scaled_capsule_half_height()); result['my_scale'] = pawn.get_editor_property('body_scale')
        result['feet_moved_cm'] = round(pawn.get_actor_location().z - cap.get_scaled_capsule_half_height() - st['feet0'])
        return step('end')
    if st['step'] == 'end' and since > 2: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
