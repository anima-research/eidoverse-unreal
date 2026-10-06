"""Style tokens, live: pick a rose accent and a slate panel from settings, the interface
rebuilds in them (chat lines survive), shot; then back to defaults. STYLE_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['STYLE_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    w = editor.get_game_world()
    for n in ('Accent', 'Panel'):
        if w: unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.UI.Style.%s ""' % n)
    unreal.log_warning('STYLE_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 80: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud')
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-style'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        b.say('a line that must survive the rebuild')
        return step('style')
    if st['step'] == 'style' and since > 1.5:
        st['lines'] = hud.get_visible_chat_line_count()
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.UI.Style.Accent ff9fc8')
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.UI.Style.Panel 10141c')
        b.rebuild_hud(); return step('shot')
    if st['step'] == 'shot' and since > 2:
        hud = b.get_editor_property('hud'); hud.show_frame('chat', True)
        result['lines_before'] = st['lines']; result['lines_after'] = hud.get_visible_chat_line_count()
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
