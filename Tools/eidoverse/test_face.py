"""Close-up of a peer's face: a still pose_bot.ts wearing FACE_AVATAR stands 1.2 m
ahead; the local explorer, in first person, looks at its head. Screenshot + FACE_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['FACE_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('FACE_RESULT ' + json.dumps(result, default=str))
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
        b.join_from_door(SERVER, WORLD, 'unreal-face-test')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and pawn and b.get_editor_property('loaded_avatar_count') >= 1 and since > 6:
        pawn.set_camera_distance(0)   # first person
        peers = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor)
                 if a != pawn and a.get_components_by_class(unreal.EidoverseAvatarMesh)]
        if not peers: return
        st['peer'] = peers[0]; st['shots'] = 0
        return step('shot')
    if st['step'] == 'shot':
        # Track the head every frame (the bot may be walking); a shot each second.
        head = st['peer'].get_actor_location() + unreal.Vector(0, 0, 150)
        eye = pawn.get_actor_location() + unreal.Vector(0, 0, 57)
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, head))
        if since > 1.5 + st['shots'] * 1.0:
            unreal.SystemLibrary.execute_console_command(w, 'shot showui')
            meshes = st['peer'].get_components_by_class(unreal.EidoverseAvatarMesh)
            result.setdefault('states', []).append(meshes[0].get_editor_property('animation_state') if meshes else None)
            st['shots'] += 1
            if st['shots'] >= int(os.environ.get('FACE_SHOTS', '1')): return step('end')
        return
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
