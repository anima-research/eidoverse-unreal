"""Live PIE check of the World frame sections (avatar, sky, ground, scene).

Joins a fresh world on an isolated preview server (first joiner owns it), applies
a sky preset and terrain, switches avatar, and screenshots each section.
Logs WORLD_PANELS_RESULT <json>.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'ui-owner-test'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('WORLD_PANELS_RESULT ' + json.dumps(result, default=str))
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
    def shot(name):
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append(name)
    def scene():
        s = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
        return s[0] if s else None
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-owner')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and scene() and since > 4:
        result['rights'] = {'role': b.get_editor_property('world_role'), 'open': b.get_editor_property('world_open'),
                            'owner': b.is_owner(), 'build': b.can_build()}
        result['avatars'] = [c.name for c in b.get_editor_property('avatar_choices')][:12]
        result['hour_before'] = scene().get_editor_property('weather_hour')
        hud.select_world_tab(2)
        hud.world_action('sky:preset:dusk')
        hud.world_action('sky:weather:overcast')
        return step('sky-shot')
    if st['step'] == 'sky-shot' and since > 1:
        shot('sky-draft')
        return step('sky-apply')
    if st['step'] == 'sky-apply' and since > 1:
        hud.world_action('sky:apply')
        return step('sky-check')
    if st['step'] == 'sky-check' and since > 3:
        result['hour_after'] = scene().get_editor_property('weather_hour')
        result['weather_after'] = scene().get_editor_property('weather_state')
        hud.select_world_tab(3)
        hud.world_action('ground:shape:rugged')
        hud.world_action('ground:tint:arid')
        hud.world_action('ground:apply')
        hud.world_action('ground:plant:meadow')
        return step('ground-shot')
    if st['step'] == 'ground-shot' and since > 4:
        shot('ground')
        return step('avatar-tab')
    if st['step'] == 'avatar-tab' and since > 1:
        hud.select_world_tab(1)
        return step('avatar-shot')
    if st['step'] == 'avatar-shot' and since > 3:
        shot('avatar')
        choices = b.get_editor_property('avatar_choices')
        current = b.get_editor_property('avatar_path')
        pick = next((c for c in choices if c.path.split('?')[0].split('/')[-1] != current.split('?')[0].split('/')[-1]), None)
        result['avatar_before'] = current
        result['scene_before'] = scene().get_name() if scene() else None
        if pick:
            hud.world_action('avatar:' + pick.path)
            result['avatar_pick'] = pick.path
        return step('avatar-check')
    if st['step'] == 'avatar-check' and since > 4:
        result['avatar_after'] = b.get_editor_property('avatar_path')
        result['joined_after_avatar'] = b.get_editor_property('joined')
        result['scene_after'] = scene().get_name() if scene() else None
        hud.select_world_tab(4)
        return step('scene-shot')
    if st['step'] == 'scene-shot' and since > 2:
        shot('scene')
        return step('end')
    if st['step'] == 'end' and since > 1.5:
        r = result
        ok = (r['rights']['owner'] and abs(r.get('hour_after', 0) - 19.4) < .2 and r.get('joined_after_avatar')
              and r.get('scene_after') == r.get('scene_before') and (not r.get('avatar_pick') or r['avatar_after'] == r['avatar_pick']))
        done(ok, '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
