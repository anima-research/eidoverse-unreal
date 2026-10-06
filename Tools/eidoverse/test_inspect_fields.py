"""Inspector transform fields + scene Find, live: spawn a crate, type a transform, the
server places it there; Find narrows the scene list. INSPECT_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', os.environ['INSPECT_WORLD'], 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('INSPECT_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 90: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud')
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-inspector'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        hud.show_frame('build', True); hud.search_library('crate'); hud.build_action('build:spawn:' + LIB); return step('spawned')
    if st['step'] == 'spawned' and since > 3:
        st['eid'] = hud.get_last_spawned_entity(); hud.select_entity(st['eid'])
        result['applied'] = hud.apply_inspect_transform(unreal.Vector(1.5, 0.25, -2.0), 90, 1.5)
        return step('placed')
    if st['step'] == 'placed' and since > 2:
        sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)[0]
        a = sc.get_entity_actor(st['eid'])
        o = b.get_editor_property('origin_centimeters')
        if a:
            l = a.get_actor_location(); r = a.get_actor_rotation(); s = a.get_actor_scale3d()
            result['placed_cm'] = [round(l.x - o.x), round(l.y - o.y), round(l.z - o.z)]; result['yaw'] = round(r.yaw); result['scale'] = round(s.x, 2)
        hud.show_frame('world', True); hud.world_action('wtab:4')
        return step('find')
    if st['step'] == 'find' and since > 2:
        result['list_all'] = hud.get_scene_list_count()
        hud.set_scene_find('zzz-none'); return step('find2')
    if st['step'] == 'find2' and since > 1.5:
        result['list_none'] = hud.get_scene_list_count()
        hud.set_scene_find('crate'); return step('find3')
    if st['step'] == 'find3' and since > 1.5:
        result['list_crate'] = hud.get_scene_list_count(); return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
