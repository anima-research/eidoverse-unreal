"""Chat markdown, live: markup conversion plus a shot of rendered lines. MD_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['MD_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('MD_RESULT ' + json.dumps(result, default=str))
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
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        result['markup'] = [unreal.EidoverseHUD.chat_markup(t) for t in ['a **bold** and *it* and _it2_ and `x<y`', 'snake_case_name and 2*3*4', '<b>not markup</b>']]
        b.join_from_door(SERVER, WORLD, 'unreal-md-test'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        for f in ('world', 'settings', 'build', 'people', 'emotes'): hud.show_frame(f, False)
        b.say('this is **bold**, this is *italic*, and `some_code()` — plain snake_case stays')
        return step('say2')
    if st['step'] == 'say2' and since > 1: b.say('<b>tags are text</b> & so is _this_ one'); return step('shot')
    if st['step'] == 'shot' and since > 3:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
