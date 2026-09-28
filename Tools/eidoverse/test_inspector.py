"""Live PIE check of the scene inspector (server `inspect` + `comp` verbs).

Fresh world: spawn a crate, inspect it from the scene tab, add an invented
component and a lock, remove the lock, open the JSON editor, undo the spawn.
Logs INSPECTOR_RESULT <json>; screenshots in Saved/Screenshots.
"""
import json
import time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', 'ui-inspect-%d' % (time.time() % 100000), 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('INSPECTOR_RESULT ' + json.dumps(result, default=str))
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
    def comps():
        text = b.get_inspected_components()
        return json.loads(text) if text else None
    eid = hud.get_last_spawned_entity()
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-inspector')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and unreal.GameplayStatics.get_player_pawn(w, 0) and since > 4:
        hud.show_frame('build', False)
        hud.build_action('build:spawn:' + LIB)
        return step('spawned')
    if st['step'] == 'spawned' and since > 3:
        hud.select_world_tab(4)
        hud.world_action('scene:select:' + eid)
        return step('inspect')
    if st['step'] == 'inspect' and since > 2:
        result['initial'] = comps()
        result['inspected_id'] = b.get_editor_property('inspected_id') == eid
        hud.set_component_json(eid, 'note', '{"text": "left here by the Unreal inspector", "n": 3}')
        return step('lock')
    if st['step'] == 'lock' and since > 1.5:
        result['with_note'] = comps()
        hud.set_component_json(eid, 'lock', 'true')
        return step('locked')
    if st['step'] == 'locked' and since > 1.5:
        result['with_lock'] = comps()
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('inspector')
        hud.set_component_json(eid, 'lock', '')
        return step('unlocked')
    if st['step'] == 'unlocked' and since > 1.5:
        result['unlocked'] = comps()
        result['bad_json_sent'] = hud.set_component_json(eid, 'note', '{nope')
        hud.world_action('comp:edit:note')
        return step('editor')
    if st['step'] == 'editor' and since > 1:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('editor')
        hud.world_action('comp:cancel')
        hud.build_action('build:undo')
        return step('check')
    if st['step'] == 'check' and since > 1.5:
        r = result
        ok = (r['initial'] == {} and r['inspected_id']
              and (r['with_note'] or {}).get('note', {}).get('n') == 3
              and (r['with_lock'] or {}).get('lock') is True and 'note' in r['with_lock']
              and r['unlocked'] is not None and 'lock' not in r['unlocked'] and 'note' in r['unlocked']
              and r['bad_json_sent'] is False)
        done(bool(ok), '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
