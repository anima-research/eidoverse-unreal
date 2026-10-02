"""Live PIE look at the idle land pose for a VRM 1.0 and a VRM 0.x body.

Needs the preview server with both avatars in its library (mythos-wings, shino).
Logs LAND_POSE_RESULT <json>; screenshots in Saved/Screenshots.
"""
import json
import time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'ui-pose-%d' % (time.time() % 100000)
import os
BODIES = [(os.path.basename(p), p) for p in os.environ.get('POSE_BODIES', 'eidoverse/assets/vrms/mythos-wings.vrm,eidoverse/assets/vrms/shino.vrm').split(',')]
FACE = os.environ.get('POSE_FACE') == '1'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'body': 0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('LAND_POSE_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 200:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    hud = b.get_editor_property('hud')
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        for f in ('chat', 'world', 'settings', 'build', 'people', 'emotes'):
            hud.show_frame(f, False)
        b.set_editor_property('avatar_path', BODIES[0][1])
        b.join_from_door(SERVER, WORLD, 'unreal-pose-test')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and pawn and since > 3:
        label, path = BODIES[st['body']]
        if st['body'] > 0:
            b.change_avatar(path)
        return step('load')
    if st['step'] == 'load':
        bodies = pawn.get_components_by_class(unreal.EidoverseAvatarMesh) if pawn else []
        if bodies and since > 8:
            if FACE:
                # A fixed camera 60 cm in front of the face, at eye level, looking back.
                loc = pawn.get_actor_location(); fwd = pawn.get_actor_forward_vector()
                eye = loc + unreal.Vector(0, 0, 70)
                cam_loc = eye + fwd * 60
                cam = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.CameraActor, cam_loc) if False else w.spawn_actor(unreal.CameraActor, cam_loc, unreal.MathLibrary.find_look_at_rotation(cam_loc, eye)) if hasattr(w, 'spawn_actor') else None
                if cam is None:
                    cam = unreal.GameplayStatics.begin_deferred_actor_spawn_from_class(w, unreal.CameraActor, unreal.Transform(cam_loc, unreal.MathLibrary.find_look_at_rotation(cam_loc, eye)))
                    unreal.GameplayStatics.finish_spawning_actor(cam, unreal.Transform(cam_loc, unreal.MathLibrary.find_look_at_rotation(cam_loc, eye)))
                cam.camera_component.set_editor_property('field_of_view', 40)
                pc.set_view_target_with_blend(cam)
                st['cam'] = cam
            pawn.set_camera_distance(0.75 if FACE else 3.2)
            # FACE: look back at the body from in front (view along -forward), level.
            yaw = pawn.get_actor_rotation().yaw + (180 if FACE else 0)
            if not FACE:
                pc.set_control_rotation(unreal.Rotator(roll=0, pitch=-5, yaw=yaw))
            return step('back')
        return
    if st['step'] == 'back' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append(BODIES[st['body']][0] + '-back')
        pc.set_control_rotation(unreal.Rotator(roll=0, pitch=-5, yaw=pawn.get_actor_rotation().yaw + 90))
        return step('side')
    if st['step'] == 'side' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append(BODIES[st['body']][0] + '-side')
        st['body'] += 1
        if st['body'] < len(BODIES):
            return step('wait')
        if FACE:
            return step('end')
        # Then a hovering swimmer in the water world, same camera.
        b.join_from_door(SERVER, 'water-preview', 'unreal-pose-test')
        return step('water')
    if st['step'] == 'water' and b.get_editor_property('joined') and pawn and since > 12:
        pawn.set_camera_distance(3.2)
        pc.set_control_rotation(unreal.Rotator(roll=0, pitch=-5, yaw=pawn.get_actor_rotation().yaw))
        result['water_mode'] = pawn.get_editor_property('locomotion_mode')
        return step('water-shot')
    if st['step'] == 'water-shot' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('water-' + str(result.get('water_mode')))
        return step('end')
    if st['step'] == 'end' and since > 1.5:
        done(True)


handle = unreal.register_slate_post_tick_callback(tick)
