"""Live PIE check of nameplates and speech bubbles.

Run a visitor that hovers in front of `unreal-bubble-test` and chats
(e.g. /private/tmp visitor.ts), with the water preview on 8993. Logs
NAMEPLATE_RESULT <json> and saves a UI screenshot.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('NAMEPLATE_RESULT ' + json.dumps(result, default=str))
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
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door('ws://127.0.0.1:8993/ws', 'water-preview', 'unreal-bubble-test')
        return step('wait')
    if st['step'] == 'wait':
        if b.get_editor_property('joined') and 'web-visitor' in list(b.get_participants()) and since > 20:
            pc = unreal.GameplayStatics.get_player_controller(w, 0)
            pc.set_control_rotation(unreal.Rotator(0, 0, 0))
            return step('look')
        return
    if st['step'] == 'look' and since > 2:
        comps = []
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
            comps += [c for c in a.get_components_by_class(unreal.WidgetComponent)]
        result['nameplates'] = len(comps)
        result['participants'] = list(b.get_participants())
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        return step('end')
    if st['step'] == 'end' and since > 1.5:
        done(result.get('nameplates', 0) >= 1)


handle = unreal.register_slate_post_tick_callback(tick)
