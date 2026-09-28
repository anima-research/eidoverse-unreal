"""Live PIE check of build mode against an isolated preview server.

Fresh owned world: spawn a library model in front of the pawn, select it
(overlay), turn / raise / size it, undo, remove, undo the removal. Each edit is a
verb whose result comes back through render-scene. Logs BUILD_MODE_RESULT <json>.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', 'ui-build-test', 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {'events': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('BUILD_MODE_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 170:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    hud = b.get_editor_property('hud')
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    def entity(eid):
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, eid):
            return a
        return None
    def pose(eid):
        a = entity(eid)
        if not a: return None
        l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
        return {'x': round(l.x), 'y': round(l.y), 'z': round(l.z), 'yaw': round(r.yaw, 1), 'scale': round(s.x, 3)}
    eid = hud.get_last_spawned_entity()
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-builder')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and unreal.GameplayStatics.get_player_pawn(w, 0) and since > 4:
        hud.show_frame('build', True)
        hud.search_library('crate')
        hud.build_action('build:spawn:' + LIB)
        return step('spawned')
    if st['step'] == 'spawned' and since > 3:
        result['spawn'] = pose(eid)
        hud.select_entity(eid)
        result['selected'] = hud.get_selected_entity()
        return step('shot')
    if st['step'] == 'shot' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        return step('edit')
    if st['step'] == 'edit' and since > 1:
        hud.build_action('build:turn:45')
        return step('raise')
    if st['step'] == 'raise' and since > 1.5:
        result['turned'] = pose(eid)
        hud.build_action('build:raise:0.5')
        return step('size')
    if st['step'] == 'size' and since > 1.5:
        result['raised'] = pose(eid)
        hud.build_action('build:size:1.5')
        return step('undo')
    if st['step'] == 'undo' and since > 1.5:
        result['sized'] = pose(eid)
        hud.build_action('build:undo')
        return step('remove')
    if st['step'] == 'remove' and since > 1.5:
        result['undone_size'] = pose(eid)
        hud.build_action('build:remove')
        return step('restore')
    if st['step'] == 'restore' and since > 1.5:
        result['after_remove'] = pose(eid)
        hud.build_action('build:undo')
        return step('check')
    if st['step'] == 'check' and since > 2:
        result['restored'] = pose(eid)
        r = result
        ok = (r['spawn'] and r['selected'] == eid and r['turned'] and abs(((r['turned']['yaw'] - r['spawn']['yaw']) % 360) - 315) < 2
              and r['raised']['z'] - r['turned']['z'] == 50 and abs(r['sized']['scale'] - 1.5) < .01
              and abs(r['undone_size']['scale'] - 1) < .01 and r['after_remove'] is None and r['restored'] is not None)
        done(bool(ok), '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
