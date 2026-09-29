"""Live PIE join of a busy world, measuring hitches (EidoverseLoadTrace).

Run Tools/eidoverse/stress_world.ts against the preview server first (it keeps
16 avatar bots walking and ~40 heavy models in WORLD). Logs STRESS_JOIN_RESULT
<json> including EIDOVERSE_LOADSTATS; every hitch is its own EIDOVERSE_HITCH line.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'stress-probe'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'frames': 0, 'worst': 0.0, 'over100': 0, 'over250': 0}
result = {}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('STRESS_JOIN_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 400:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    if st['step'] in ('loading', 'settle'):
        st['frames'] += 1
        st['worst'] = max(st['worst'], dt * 1000)
        st['over100'] += dt > .1
        st['over250'] += dt > .25
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.LoadStatsReset')
        b.set_editor_property('avatar_path', 'eidoverse/assets/vrms/claude.vrm')
        b.join_from_door(SERVER, WORLD, 'unreal-stress')
        return step('loading')
    if st['step'] == 'loading':
        scenes = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
        s = scenes[0] if scenes else None
        models = s.get_editor_property('loaded_models') + s.get_editor_property('failed_models') if s else 0
        entities = s.get_editor_property('entity_count') if s else 0
        avatars = b.get_editor_property('loaded_avatar_count')
        remote = b.get_editor_property('remote_count')
        complete = entities and models >= entities and avatars >= remote and remote > 0
        if b.get_editor_property('joined') and since > 5 and (complete or since > 150):
            result['complete'] = bool(complete)
            result['load_seconds'] = round(since, 1)
            result['counts'] = {'entities': entities, 'models': models, 'avatars': avatars, 'remote': remote}
            unreal.SystemLibrary.execute_console_command(w, 'shot showui')
            return step('settle')
        if int(since) % 10 == 0 and int(since) != st.get('logged'):
            st['logged'] = int(since)
            unreal.log_warning('STRESS_PROGRESS t=%d entities=%d models=%d avatars=%d/%d' % (since, entities, models, avatars, remote))
        return
    if st['step'] == 'settle' and since > 10:
        result['frames'] = {'n': st['frames'], 'worst_ms': round(st['worst']), 'over_100ms': st['over100'], 'over_250ms': st['over250']}
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.LoadStats')
        return step('end')
    if st['step'] == 'end' and since > 1:
        done(True)


handle = unreal.register_slate_post_tick_callback(tick)
