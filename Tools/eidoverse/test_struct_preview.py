"""Structure editor preview, live: a new building, the room tool, the mouse on the
ground: the footprint grid and the hovered cell draw (and a wall tool shows an edge).
STRUCTPREV_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['SP_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('STRUCTPREV_RESULT ' + json.dumps(result, default=str))
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
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-structure'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        for f in ('chat', 'world', 'settings', 'people', 'emotes'): hud.show_frame(f, False)
        pc.set_control_rotation(unreal.Rotator(0, -35, 0))
        hud.show_frame('build', True); hud.build_action('struct:new'); return step('aim')
    if st['step'] == 'aim' and since > 3:
        hud.set_pointer_mode(True); pc.set_mouse_location(820, 620); return step('room')
    if st['step'] == 'room' and since > 2:
        result['room'] = [hud.get_struct_preview_count(i) for i in range(3)]
        unreal.SystemLibrary.execute_console_command(w, 'shot'); return step('wall')
    if st['step'] == 'wall' and since > 1.5:
        hud.build_action('struct:tool:wall'); return step('wall2')
    if st['step'] == 'wall2' and since > 2:
        result['wall'] = [hud.get_struct_preview_count(i) for i in range(3)]
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
