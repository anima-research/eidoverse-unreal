"""After chatting, the camera and walking must be back: open chat with Enter from
mouselook, type and send through Slate (Eidoverse.TestHUDChat), then check that
look and move input are not ignored and the HUD is back in mouselook.
Logs CHAT_FOCUS_RESULT <json>."""
import json, time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'ui-chatfocus-%d' % (time.time() % 100000)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('CHAT_FOCUS_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def state(hud, pc):
    return {'pointer': hud.is_pointer_mode(), 'look_ignored': pc.is_look_input_ignored(), 'move_ignored': pc.is_move_input_ignored()}
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 120: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud'); pc = unreal.GameplayStatics.get_player_controller(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-chat-focus')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        hud.set_pointer_mode(False)
        result['before'] = state(hud, pc)
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.TestHUDChat')
        return step('chatting')
    if st['step'] == 'chatting' and since > 4:
        result['after_send'] = state(hud, pc)
        hud.set_pointer_mode(True)
        hud.focus_chat()
        result['while_typing'] = state(hud, pc)
        hud.set_pointer_mode(False)   # what Esc / a world click do (LeaveChat -> mouselook)
        return step('check')
    if st['step'] == 'check' and since > 1:
        result['after_leave'] = state(hud, pc)
        clean = lambda s: not s['pointer'] and not s['look_ignored'] and not s['move_ignored']
        ok = clean(result['before']) and clean(result['after_send']) and clean(result['after_leave']) and result['while_typing']['move_ignored']
        done(ok, '' if ok else 'input left blocked')
handle = unreal.register_slate_post_tick_callback(tick)
