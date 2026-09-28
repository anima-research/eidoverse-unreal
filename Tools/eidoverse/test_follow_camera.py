"""Live PIE check of the follow camera and the player's own avatar.

Needs an isolated server, e.g. `bun tools/water/preview.ts <bundle> 8993`.
Logs FOLLOW_CAMERA_RESULT <json>; UI-inclusive screenshots in Saved/Screenshots.
"""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'water-preview'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'door', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('FOLLOW_CAMERA_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def view(w, pawn):
    cam = unreal.GameplayStatics.get_player_camera_manager(w, 0)
    loc = cam.get_camera_location()
    head = pawn.get_actor_location() + unreal.Vector(0, 0, 57)
    body = [c for c in pawn.get_components_by_class(unreal.EidoverseAvatarMesh)]
    return {'cam_to_head_cm': round((loc - head).length(), 1),
            'first_person': pawn.get_editor_property('first_person'),
            'distance': pawn.get_editor_property('camera_distance'),
            'body': len(body), 'body_visible': bool(body and body[0].is_visible())}


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 180:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if st['step'] == 'door' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        # The preview library holds one body; the default (claude.vrm) would 404 there.
        b.set_editor_property('avatar_path', 'eidoverse/assets/vrms/mythos-wings.vrm')
        b.join_from_door(SERVER, WORLD, 'unreal-camera-test')
        return step('joining')
    if st['step'] == 'joining':
        scenes = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
        if int(since) != st.get('logged'):
            st['logged'] = int(since)
            unreal.log_warning('FOLLOW_CAMERA_WAIT ' + json.dumps({
                'joined': b.get_editor_property('joined'), 'pawn': pawn.get_class().get_name() if pawn else None,
                'models': scenes[0].get_editor_property('loaded_models') if scenes else None,
                'bodies': len(pawn.get_components_by_class(unreal.EidoverseAvatarMesh)) if pawn else None,
                'all': [c.get_class().get_name() for c in pawn.get_components_by_class(unreal.ActorComponent)] if pawn else None}))
        if (b.get_editor_property('joined') and pawn and scenes and scenes[0].get_editor_property('loaded_models') >= 5
                and pawn.get_components_by_class(unreal.EidoverseAvatarMesh) and since > 5):
            result['third'] = view(w, pawn)
            unreal.SystemLibrary.execute_console_command(w, 'shot showui')
            result['shots'].append('third')
            b.emote('wave')
            return step('emote')
        return
    if st['step'] == 'emote' and since > 0.8:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('emote')
        pawn.set_camera_distance(9)
        return step('far')
    if st['step'] == 'far' and since > 1.5:
        result['far'] = view(w, pawn)
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('far')
        for _ in range(25):
            pawn.zoom(1)
        return step('first')
    if st['step'] == 'first' and since > 1.5:
        result['first'] = view(w, pawn)
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('first')
        pawn.zoom(-1)   # 0.4 m: still first person (hysteresis)
        result['hysteresis_fp'] = pawn.get_editor_property('first_person')
        pawn.zoom(-10)
        return step('back')
    if st['step'] == 'back' and since > 1.5:
        result['back'] = view(w, pawn)
        t, f, fp, bk = result['third'], result['far'], result['first'], result['back']
        ok = (not t['first_person'] and t['body'] == 1 and t['body_visible'] and 300 < t['cam_to_head_cm'] < 480
              and f['cam_to_head_cm'] > t['cam_to_head_cm']
              and fp['first_person'] and not fp['body_visible'] and fp['cam_to_head_cm'] < 30
              and result['hysteresis_fp'] and not bk['first_person'] and bk['body_visible'])
        done(ok, '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
