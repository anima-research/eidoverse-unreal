"""Live PIE look at the UMG shell against an isolated preview server.

    bun tools/water/preview.ts <bundle> 19350     (eidoverse-worlds)
    UnrealEditor Test1.uproject -ExecutePythonScript=Tools/eidoverse/test_ui_shell.py

Joins water-preview, chats, opens frames, the ∃ menu and help, and saves
UI-inclusive screenshots (Saved/Screenshots). Logs UI_SHELL_RESULT <json>.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

PORT = 19350
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'connect', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('UI_SHELL_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, 'shot showui')
    result['shots'].append(name)


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 150:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges:
        return
    b = bridges[0]
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    if st['step'] == 'connect':
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.disconnect()
        for k, v in (('endpoint', f'ws://127.0.0.1:{PORT}/ws'), ('world_name', 'water-preview'),
                     ('participant_name', 'unreal-ui-test'), ('join_token', ''), ('identity_token', ''),
                     ('render_dynamic_world', True), ('avatar_library_base', f'http://127.0.0.1:{PORT}/library/')):
            b.set_editor_property(k, v)
        b.connect()
        return step('wait-join')
    hud = b.get_editor_property('hud')
    if st['step'] == 'wait-join':
        scenes = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
        if b.get_editor_property('joined') and hud and scenes and scenes[0].get_editor_property('loaded_models') >= 5:
            result['hud_class'] = hud.get_class().get_name()
            result['say_ok_at_join'] = b.say('Hello from the Unreal client')
            return step('settle')
        return
    if st['step'] == 'settle' and since > 4:
        result['say_ok_later'] = b.say('Hello again, @unreal-ui-test')
        result['status'] = b.get_editor_property('connection_status')
        hud.show_frame('people', True)
        hud.show_frame('emotes', True)
        hud.toast('Toasts look like this', False)
        hud.hint('swimming — WASD move, Space rise, C / Ctrl dive, Shift faster', 6)
        return step('shot-main')
    if st['step'] == 'shot-main' and since > 1.5:
        shot(w, 'ui_main')
        return step('menu')
    if st['step'] == 'menu' and since > 1:
        hud.toggle_menu()
        return step('shot-menu')
    if st['step'] == 'shot-menu' and since > 1:
        shot(w, 'ui_menu')
        return step('help')
    if st['step'] == 'help' and since > 1:
        hud.toggle_menu()
        hud.toggle_help()
        return step('shot-help')
    if st['step'] == 'shot-help' and since > 1.5:
        shot(w, 'ui_help')
        result['participants'] = list(b.get_participants())
        result['http_base'] = b.http_base()
        return step('finish')
    if st['step'] == 'finish' and since > 2:
        hud.toggle_help()
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.TestHUDChat')
        return step('keyboard')
    if st['step'] == 'keyboard' and since > 3:
        shot(w, 'ui_keyboard_chat')
        return step('end')
    if st['step'] == 'end' and since > 1.5:
        done(True)


handle = unreal.register_slate_post_tick_callback(tick)
