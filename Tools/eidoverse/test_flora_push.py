"""Flora pushers + keep(d), live: plant a meadow, read MPC_EidoverseFlora's Pusher0 (my
feet, r 110), shoot close at my feet (blades part) and wide (thinning to 90 m). FLORA_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['FLORA_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('FLORA_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 150: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud')
    pc = unreal.GameplayStatics.get_player_controller(w, 0); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        for f in ('chat', 'world', 'settings', 'build', 'people', 'emotes'): hud.show_frame(f, False)
        b.join_from_door(SERVER, WORLD, 'unreal-flora'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and pawn and since > 5:
        hud.world_action('ground:plant:meadow'); hud.show_frame('world', False); return step('grow')
    if st['step'] == 'grow' and since > 10:
        mpc = unreal.load_asset('/Eidoverse/FX/MPC_EidoverseFlora')
        v = unreal.MaterialLibrary.get_vector_parameter_value(w, mpc, 'Pusher0')
        l = pawn.get_actor_location()
        result['pusher0'] = [round(v.r), round(v.g), round(v.b), round(v.a)]
        result['pawn'] = [round(l.x), round(l.y), round(l.z)]
        pawn.set_camera_distance(1.6)
        pc.set_control_rotation(unreal.Rotator(0, -45, 0))   # pitch down at my feet
        return step('close')
    if st['step'] == 'close' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot'); return step('wide')
    if st['step'] == 'wide' and since > 1.5:
        pawn.set_camera_distance(0); pc.set_control_rotation(unreal.Rotator(-8, 0, 0)); return step('wideshot')
    if st['step'] == 'wideshot' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot'); return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
