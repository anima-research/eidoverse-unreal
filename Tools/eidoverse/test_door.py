"""Front door, live: the roster arrives from /avatars with portraits, a pick is
remembered and worn on entering, the key field reveals. DOOR_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['DOOR_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'open', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    # leave the remembered pick as it was (a test must not choose the person's body)
    w = editor.get_game_world()
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge) if w else []
    if bs and bs[0].get_editor_property('hud'): bs[0].get_editor_property('hud').select_door_avatar('')
    unreal.log_warning('DOOR_RESULT ' + json.dumps(result, default=str))
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
    if st['step'] == 'open' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.disconnect()
        b.set_editor_property('endpoint', SERVER); b.set_editor_property('world_name', WORLD); b.set_editor_property('participant_name', 'door-tester')
        b.set_editor_property('front_door', True)
        return step('fields')
    if st['step'] == 'fields' and since > 1:
        hud.set_door_fields(SERVER, WORLD, 'door-tester')   # the local preview, never the saved server
        return step('roster')
    if st['step'] == 'roster':
        if (hud.get_door_roster_count() > 0 and hud.get_door_portrait_count() > 0 and since > 3) or since > 25:
            result['roster'] = hud.get_door_roster_count(); result['portraits'] = hud.get_door_portrait_count()
            hud.select_door_avatar('shino'); hud.show_door_key(True)
            return step('shot')
    if st['step'] == 'shot' and since > 1.5:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('enter')
    if st['step'] == 'enter' and since > 1.5:
        result['picked'] = hud.get_door_avatar()
        hud.show_door_key(False); hud.door_enter(); return step('joined')
    if st['step'] == 'joined':
        if b.get_editor_property('joined') or since > 20:
            result['joined'] = b.get_editor_property('joined'); result['avatar_path'] = b.get_editor_property('avatar_path')
            result['endpoint'] = b.get_editor_property('endpoint')
            return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
