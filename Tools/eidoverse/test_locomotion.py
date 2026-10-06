"""Climb routes, helm and mantle, live. A crate with a climb route, a crate with a helm,
and a 1 m block to mantle. LOCO_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['LOCO_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('LOCO_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def actor(w, eid):
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
        if a.tags and str(a.tags[0]) == eid: return a
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 160: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    o = b.get_editor_property('origin_centimeters')
    def wire(x, y, z): return unreal.Vector(o.x + z * 100, o.y - x * 100, o.z + y * 100)
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-loco-test'); return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and since > 12 and actor(w, 'ladderbox')): return
        pawn.set_actor_location(wire(0, 0, -1.3) + unreal.Vector(0, 0, 100), False, True)   # ladderbox at wire (0,0,-2); route foot at local (0,0,1)
        return step('climb')
    if st['step'] == 'climb' and since > 1.5:
        result['climb_started'] = b.handle_use_key()
        st['z0'] = pawn.get_actor_location().z
        return step('climbing')
    if st['step'] == 'climbing' and since > 1.0:
        result['climb_mode'] = b.get_loco_mode()
        result['climb_rose_cm'] = round(pawn.get_actor_location().z - st['z0'])
        return step('climbed')
    if st['step'] == 'climbed' and since > 3.5:
        result['after_climb_mode'] = b.get_loco_mode()
        pawn.set_actor_location(wire(4, 0, -0.5) + unreal.Vector(0, 0, 100), False, True)   # boat at wire (4,0,0), helm local (0,1,0)
        return step('helm')
    if st['step'] == 'helm' and since > 1.5:
        result['helm_claim'] = b.handle_use_key()
        return step('piloting')
    if st['step'] == 'piloting' and since > 2:
        result['pilot_mode'] = b.get_loco_mode()
        result['helm_exit'] = b.handle_use_key()
        return step('exited')
    if st['step'] == 'exited' and since > 1.5:
        result['after_exit_mode'] = b.get_loco_mode()
        pawn.set_actor_location(wire(-4, 0, -0.9) + unreal.Vector(0, 0, 95), False, True)   # step at wire (-4,0,0), 1 m tall
        pawn.set_actor_rotation(unreal.Rotator(0, 0, 0), False)
        return step('mantle')
    if st['step'] == 'mantle' and since > 1.5:
        st['mz'] = pawn.get_actor_location().z
        result['mantle'] = pawn.try_mantle()
        return step('mantled')
    if st['step'] == 'mantled' and since > 1.2:
        result['mantle_rose_cm'] = round(pawn.get_actor_location().z - st['mz'])
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
