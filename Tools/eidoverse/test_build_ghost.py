"""Live PIE check of build-mode ghost placement (web build.js holdGhost/commitSpawn).

Hold a library model as a ghost, let it follow the screen-centre aim onto the
ground, place it, and check the spawned entity lands where the ghost was, facing
the same way. Then hold again and cancel. Logs BUILD_GHOST_RESULT <json>.
"""
import json
import time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', 'ui-ghost-%d' % (time.time() % 100000), 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {'shots': []}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('BUILD_GHOST_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 170:
        return done(False, 'timeout at ' + st['step'])
    if not w:
        return
    bridges = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bridges or not bridges[0].get_editor_property('hud'):
        return
    b = bridges[0]
    hud = b.get_editor_property('hud')
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    since = st['t'] - st['at']
    def step(name):
        st['step'] = name
        st['at'] = st['t']
    def ghost():
        t = hud.get_ghost_transform()
        l, r, s = t.translation, t.rotation.rotator(), t.scale3d
        return {'x': round(l.x), 'y': round(l.y), 'z': round(l.z), 'yaw': round(r.yaw, 1), 'scale': round(s.x, 3)}
    def entity(eid):
        for a in unreal.GameplayStatics.get_all_actors_with_tag(w, eid):
            l, r = a.get_actor_location(), a.get_actor_rotation()
            return {'x': round(l.x), 'y': round(l.y), 'z': round(l.z), 'yaw': round(r.yaw, 1)}
        return None
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-ghost-builder')
        return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and unreal.GameplayStatics.get_player_pawn(w, 0) and since > 4:
        hud.show_frame('build', True)
        hud.search_library('crate')
        # Look down a little so the screen centre meets the ground a few metres out.
        pc.set_control_rotation(unreal.Rotator(roll=0, pitch=-12, yaw=30))
        hud.build_action('build:hold:' + LIB)
        result['held'] = hud.has_ghost()
        return step('aim')
    if st['step'] == 'aim' and since > 4:
        result['ghost'] = ghost()
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        result['pawn'] = {'x': round(pawn.get_actor_location().x), 'y': round(pawn.get_actor_location().y), 'z': round(pawn.get_actor_location().z)}
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('ghost')
        return step('place')
    if st['step'] == 'place' and since > 1:
        result['ghost'] = ghost()   # same frame as the place: the aim keeps tracking the camera
        result['placed'] = hud.place_ghost()
        result['after_place_has_ghost'] = hud.has_ghost()
        return step('check')
    if st['step'] == 'check' and since > 3:
        result['entity'] = entity(hud.get_last_spawned_entity())
        unreal.SystemLibrary.execute_console_command(w, 'shot showui')
        result['shots'].append('placed')
        hud.build_action('build:hold:' + LIB)
        return step('cancel')
    if st['step'] == 'cancel' and since > 1.5:
        result['cancelled'] = hud.cancel_ghost()
        hud.build_action('build:undo')   # leave the world as we found it
        result['after_cancel_has_ghost'] = hud.has_ghost()
        r, g, e = result, result['ghost'], result.get('entity')
        dyaw = abs(((e['yaw'] - g['yaw']) + 180) % 360 - 180) if e else 999
        flat = ((g['x'] - r['pawn']['x']) ** 2 + (g['y'] - r['pawn']['y']) ** 2) ** .5
        ok = (r['held'] and 150 < flat < 2001 and g['z'] < r['pawn']['z'] and r['placed'] and not r['after_place_has_ghost']
              and e is not None and abs(e['x'] - g['x']) <= 2 and abs(e['y'] - g['y']) <= 2 and abs(e['z'] - g['z']) <= 2 and dyaw < 1
              and r['cancelled'] and not r['after_cancel_has_ghost'])
        done(bool(ok), '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
