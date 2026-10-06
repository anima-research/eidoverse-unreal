"""Dragging someone else's limp body, live: limp_bot.ts lies 2 m ahead; grab its hips
along a ray, swing the aim so the body follows, then nail it. DRAGGER_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['DRAGGER_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('DRAGGER_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 120: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]
    pc = unreal.GameplayStatics.get_player_controller(w, 0); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-dragger'); return step('wait')
    if st['step'] == 'wait':
        a = b.get_peer_actor('limp-bot'); m = b.get_peer_body('limp-bot')
        if not (b.get_editor_property('joined') and a and m and m.human_bone_position('hips') and since > 8): return
        hips = m.human_bone_position('hips')
        eye = pc.player_camera_manager.get_camera_location()
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, hips))
        return step('aimed')
    if st['step'] == 'aimed' and since > .5:
        m = b.get_peer_body('limp-bot'); hips = m.human_bone_position('hips'); a = b.get_peer_actor('limp-bot')
        eye = pc.player_camera_manager.get_camera_location()   # the camera's ray, as a mouse grab uses
        st['a0'] = a.get_actor_location(); st['h0'] = hips
        result['grabbed'] = b.grab_body_at(eye, hips - eye); result['holding'] = b.get_held_body()
        return step('swing')
    if st['step'] == 'swing':
        r = pc.get_control_rotation(); pc.set_control_rotation(unreal.Rotator(r.roll, r.pitch, r.yaw + 40 * dt))
        if since > 2.5:
            a = b.get_peer_actor('limp-bot'); d = a.get_actor_location() - st['a0']
            result['moved_cm'] = [round(d.x), round(d.y), round(d.z)]
            h = b.get_peer_body('limp-bot').human_bone_position('hips'); result['hips_rise_cm'] = round(h.z - st['h0'].z)
            b.let_go_of_body(True); return step('after')
    if st['step'] == 'after' and since > 2:
        result['holding_after'] = b.get_held_body(); return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
