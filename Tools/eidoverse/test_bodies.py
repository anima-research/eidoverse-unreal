"""Remote bodies, live: a limp bot (clip 'ragdoll' + a lying pose) must render limp with
its head down at hips height; a winged bot with wingsFolded must fold; a bot mounted on
a socket must be pinned to the seat whatever p it streams. BODIES_RESULT <json>.
Bots (see test_bodies.sh): limp-bot, wing-bot, rider-bot."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['BODIES_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('BODIES_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def peer(w, name):
    b = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)[0]
    return b.get_peer_actor(name), b.get_peer_body(name)


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 200: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud')
    pc = unreal.GameplayStatics.get_player_controller(w, 0); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']

    def step(n):
        st['step'] = n; st['at'] = st['t']

    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        for f in ('chat', 'world', 'settings', 'build', 'people', 'emotes'): hud.show_frame(f, False)
        b.join_from_door(SERVER, WORLD, 'unreal-bodies-test')
        return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and b.get_editor_property('loaded_avatar_count') >= 3 and since > 10): return
        o = b.get_editor_property('origin_centimeters')
        pawn.set_actor_location(unreal.Vector(o.x - 450, o.y, 120), False, True)
        pawn.set_camera_distance(2.0)
        return step('look')
    if st['step'] == 'look':
        a, m = peer(w, 'limp-bot')
        if a: pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() + unreal.Vector(0, 0, 60), a.get_actor_location()))
        if since < 4: return
        hips = m.human_bone_position('hips') if m else None; head = m.human_bone_position('head') if m else None
        result['limp'] = {'is_limp': m.is_limp() if m and hasattr(m, 'is_limp') else None,
                          'head_minus_hips_cm': round(head.z - hips.z) if hips and head else None}
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        a2, m2 = peer(w, 'rider-bot')
        o = b.get_editor_property('origin_centimeters')
        if a2:
            l = a2.get_actor_location()
            result['rider'] = {'at': [round(l.x - o.x), round(l.y - o.y), round(l.z - o.z)], 'state': m2.get_editor_property('animation_state') if m2 else None}
        a3, m3 = peer(w, 'wing-bot')
        if a3:
            pawn.set_camera_distance(0)
            l3 = a3.get_actor_location()
            pawn.set_actor_location(unreal.Vector(l3.x - 160, l3.y - 160, l3.z + 90), False, True)
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() + unreal.Vector(0, 0, 60), a3.get_actor_location() + unreal.Vector(0, 0, 120)))
        return step('wingshot')
    if st['step'] == 'wingshot' and since > 3:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)


handle = unreal.register_slate_post_tick_callback(tick)
