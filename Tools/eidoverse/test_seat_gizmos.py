"""Seat-anchor gizmos, live: a crate gets two seats; with it selected in build mode both
show as gizmos; picking one, turning it and removing the other round-trip through the
server. SEATGIZ_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', os.environ['SG_WORLD'], 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('SEATGIZ_RESULT ' + json.dumps(result, default=str))
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
    pc = unreal.GameplayStatics.get_player_controller(w, 0); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        unreal.SystemLibrary.execute_console_command(w, 'r.SetRes 1440x900w')
        b.join_from_door(SERVER, WORLD, 'unreal-seats'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 4:
        hud.show_frame('build', True); hud.search_library('crate'); hud.build_action('build:spawn:' + LIB); return step('spawned')
    if st['step'] == 'spawned' and since > 3:
        st['eid'] = hud.get_last_spawned_entity()
        data = {'seat': {'pos': [-0.3, 0.9, 0], 'yaw': 0}, 'seat2': {'pos': [0.3, 0.9, 0], 'yaw': 1.57}}
        b.send_verb_json('comp', json.dumps({'id': st['eid'], 'type': 'sockets', 'data': data}))
        hud.select_entity(st['eid']); return step('shown')
    if st['step'] == 'shown' and since > 2:
        result['gizmos'] = hud.get_seat_gizmo_count()
        sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)[0]
        a = sc.get_entity_actor(st['eid'])
        for f in ('chat', 'world', 'settings', 'people', 'emotes'): hud.show_frame(f, False)
        if a:
            l = a.get_actor_location(); pawn.set_camera_distance(0)
            pawn.set_actor_location(unreal.Vector(l.x - 250, l.y + 120, l.z + 60), False, True)
            pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() + unreal.Vector(0, 0, 70), l + unreal.Vector(0, 0, 80)))
        hud.select_seat('seat'); return step('shot')
    if st['step'] == 'shot' and since > 2:
        unreal.SystemLibrary.execute_console_command(w, 'shot'); return step('edit')
    if st['step'] == 'edit' and since > 1.5:
        result['picked'] = hud.get_selected_seat()
        hud.build_action('tool:seat:turn:seat:15'); hud.build_action('tool:seat:remove:seat2')
        return step('after')
    if st['step'] == 'after' and since > 2.5:
        result['gizmos_after'] = hud.get_seat_gizmo_count(); result['still_picked'] = hud.get_selected_seat()
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
