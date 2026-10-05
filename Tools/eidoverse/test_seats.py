"""Seats, held poses and agent animations, live.

Run first (a fresh world each time; the bot owns it and builds the fixtures):
  SEAT_WORLD=seat-<rand> POSE_BOT_HELD='{"rightUpperArm":[0,0,-0.7071,0.7071]}' POSE_BOT_ANIM=1 \
  POSE_BOT_SAY='hello there, seat tester' POSE_BOT_VERBS='[...spawn crate A with sockets at wire (2.5,0,0),
  crate B (no sockets, small) at (-2.5,0,0)...]' bun Tools/eidoverse/pose_bot.ts ws://127.0.0.1:8993/ws $SEAT_WORLD <avatar> 200 0 1.5
The explorer joins, checks the bot's held arm is up, mounts the socket (X), gets up,
sits on the small crate (furniture), gets up, and sits on the ground. SEATS_RESULT <json>.
"""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['SEAT_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('SEATS_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def wire(b, x, z):
    o = b.get_editor_property('origin_centimeters')
    return unreal.Vector(o.x + z * 100, o.y - x * 100, o.z)


def self_state(pawn):
    ms = pawn.get_components_by_class(unreal.EidoverseAvatarMesh)
    return {'seated': pawn.get_editor_property('seated'), 'posture': pawn.get_editor_property('posture'),
            'clip': ms[0].get_editor_property('animation_state') if ms else None,
            'pos': [round(v) for v in (pawn.get_actor_location().x, pawn.get_actor_location().y, pawn.get_actor_location().z)]}


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

    def shot(name):
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result.setdefault('shots', []).append(name)

    def look(target):
        eye = pawn.get_actor_location() + unreal.Vector(0, 0, 57)
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, target))

    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        for f in ('chat', 'world', 'settings', 'build', 'people', 'emotes'): hud.show_frame(f, False)
        b.join_from_door(SERVER, WORLD, 'unreal-seat-test')
        return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and pawn and b.get_editor_property('loaded_avatar_count') >= 1 and since > 8): return
        peers = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
                 if a != pawn and a.get_components_by_class(unreal.EidoverseAvatarMesh)]
        if not peers: return
        st['peer'] = peers[0]
        pawn.set_actor_location(wire(b, 0, -1.5) + unreal.Vector(0, 0, 100), False, True)
        pawn.set_camera_distance(3.0)
        return step('held')
    if st['step'] == 'held':
        peer = st['peer']; look(peer.get_actor_location() + unreal.Vector(0, 0, 120))
        if since < 4: return
        m = peer.get_components_by_class(unreal.EidoverseAvatarMesh)[0]
        hand = m.human_bone_position('rightHand'); head = m.human_bone_position('head')
        result['held_arm_up'] = bool(hand and head and hand.z > head.z)
        result['hand_minus_head_cm'] = round(hand.z - head.z) if hand and head else None
        shot('held'); return step('to_socket')
    if st['step'] == 'to_socket':
        pawn.set_actor_location(wire(b, 2.5, -0.8) + unreal.Vector(0, 0, 100), False, True)
        if since < 1.5: return
        pawn.set_editor_property('sit_pressed', True)
        return step('mounted')
    if st['step'] == 'mounted' and since > 2.5:
        result['mounted'] = self_state(pawn)
        pawn.set_camera_distance(3.5); look(pawn.get_actor_location())
        shot('mounted'); return step('unmount')
    if st['step'] == 'unmount' and since > 1:
        pawn.set_editor_property('sit_pressed', True)
        return step('unmounted')
    if st['step'] == 'unmounted' and since > 1.5:
        result['unmounted'] = self_state(pawn)
        pawn.set_actor_location(wire(b, -2.5, -0.8) + unreal.Vector(0, 0, 100), False, True)
        return step('to_chair')
    if st['step'] == 'to_chair' and since > 1.5:
        pawn.set_editor_property('sit_pressed', True)
        return step('chair')
    if st['step'] == 'chair' and since > 2.5:
        result['chair'] = self_state(pawn)
        shot('chair')
        pawn.set_editor_property('sit_pressed', True)
        return step('off_chair')
    if st['step'] == 'off_chair' and since > 1.5:
        result['off_chair'] = self_state(pawn)
        pawn.set_actor_location(wire(b, 0, -4) + unreal.Vector(0, 0, 100), False, True)
        return step('to_ground')
    if st['step'] == 'to_ground' and since > 1.5:
        pawn.set_editor_property('sit_pressed', True)
        return step('ground')
    if st['step'] == 'ground' and since > 2:
        result['ground'] = self_state(pawn)
        return step('end')
    if st['step'] == 'end' and since > 1: done(True)


handle = unreal.register_slate_post_tick_callback(tick)
