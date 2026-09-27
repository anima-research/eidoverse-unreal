"""Live PIE check of the general-purpose client map (front door, join, disconnect).

Needs an isolated server, e.g. `bun tools/water/preview.ts <bundle> 8993`.
Logs FRONT_DOOR_RESULT <json>; UI-inclusive screenshots in Saved/Screenshots.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'water-preview'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'offline', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('FRONT_DOOR_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 150:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if st['step'] == 'offline' and since > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        result['offline'] = {'game_mode': unreal.GameplayStatics.get_game_mode(w).get_class().get_name(),
                             'pawn': pawn.get_class().get_name() if pawn else None,
                             'scenes': len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)),
                             'oceans': len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.SpectralOcean)),
                             'front_door': b.get_editor_property('front_door'),
                             'status': b.get_editor_property('connection_status')}
        return step('shot-door')
    if st['step'] == 'shot-door' and since > 1:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('door')
        result['join_started'] = b.join_from_door(SERVER, WORLD, 'unreal-door-test')
        return step('joining')
    if st['step'] == 'joining':
        scenes = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
        if b.get_editor_property('joined') and scenes and scenes[0].get_editor_property('loaded_models') >= 5 and since > 4:
            result['joined'] = {'pawn': pawn.get_class().get_name() if pawn else None,
                                'mode': pawn.get_editor_property('locomotion_mode') if pawn else None,
                                'water_volumes': scenes[0].get_editor_property('water_volume_count')}
            unreal.SystemLibrary.execute_console_command(w, 'shot showui')
            result['shots'].append('joined')
            return step('leave')
        return
    if st['step'] == 'leave' and since > 1.5:
        b.disconnect()
        return step('left')
    if st['step'] == 'left' and since > 2:
        result['after_leave'] = {'pawn': pawn.get_class().get_name() if pawn else None,
                                 'scenes': len(unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)),
                                 'status': b.get_editor_property('connection_status')}
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('left')
        return step('end')
    if st['step'] == 'end' and since > 1.5:
        o, j, a = result['offline'], result.get('joined', {}), result['after_leave']
        ok = (o['game_mode'] == 'EidoverseGameMode' and o['pawn'] is None and o['scenes'] == 0 and o['oceans'] == 0
              and j.get('pawn') == 'EidoverseExplorer' and j.get('water_volumes') == 1
              and a['pawn'] is None and a['scenes'] == 0)
        done(ok, '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
