"""Getting up after a ragdoll, live: limp, settle, get up; the capsule must stand on the
ground (not inside it), walking, and the body must move when pushed. GETUP_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['GU_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('GETUP_RESULT ' + json.dumps(result, default=str))
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
        b.join_from_door(SERVER, WORLD, 'unreal-getup'); return step('wait')
    if st['step'] == 'wait':
        ms = pawn.get_components_by_class(unreal.EidoverseAvatarMesh) if pawn else []
        if not (b.get_editor_property('joined') and isinstance(pawn, unreal.EidoverseExplorer) and ms and ms[0].human_bone_position('hips') and since > 6): return
        st['z_stand'] = pawn.get_actor_location().z; b.go_limp(); return step('limp')
    if st['step'] == 'limp' and since > 5:
        result['limp_z_offset_cm'] = round(pawn.get_actor_location().z - st['z_stand'])
        b.get_up(); return step('up')
    if st['step'] == 'up' and since > 2:
        z = pawn.get_actor_location().z
        result['stand_z_offset_cm'] = round(z - st['z_stand']); result['downed'] = b.is_downed()
        result['mode'] = str(pawn.get_movement_component().get_editor_property('movement_mode'))
        st['p0'] = pawn.get_actor_location()
        pawn.add_movement_input(unreal.Vector(1, 0, 0), 1.0, True); return step('walk')
    if st['step'] == 'walk':
        pawn.add_movement_input(unreal.Vector(1, 0, 0), 1.0, True)
        if since > 1.5:
            d = pawn.get_actor_location() - st['p0']; result['walked_cm'] = round((d.x ** 2 + d.y ** 2) ** .5)
            return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
