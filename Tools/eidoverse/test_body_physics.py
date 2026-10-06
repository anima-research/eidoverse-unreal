"""My body's physics, live: go limp (R) and settle lying down, get up; a shove from a
bot knocks me over (pushable); flight with a winged body after /grant +fly. BODY_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['BODY_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('BODY_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def me(w):
    p = unreal.GameplayStatics.get_player_pawn(w, 0)
    ms = p.get_components_by_class(unreal.EidoverseAvatarMesh) if p else []
    return p, (ms[0] if ms else None)
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 240: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.set_editor_property('avatar_path', 'eidoverse/assets/vrms/mythos-wings.vrm')
        b.join_from_door(SERVER, WORLD, 'unreal-body-test'); return step('wait')
    if st['step'] == 'wait':
        p, m = me(w)
        if not (b.get_editor_property('joined') and m and m.human_bone_position('hips') and since > 10): return
        b.go_limp(); return step('limp')
    if st['step'] == 'limp' and since > 4:
        p, m = me(w)
        hips = m.human_bone_position('hips'); head = m.human_bone_position('head')
        result['limp'] = {'is_limp': m.is_limp(), 'head_minus_hips_cm': round(head.z - hips.z) if hips and head else None}
        b.get_up(); return step('up')
    if st['step'] == 'up' and since > 2:
        p, m = me(w)
        result['after_get_up_limp'] = m.is_limp()
        b.handle_interaction_command('/grant unreal-body-test owner +fly')
        return step('granted')
    if st['step'] == 'granted' and since > 2:
        p, m = me(w)
        st['z0'] = p.get_actor_location().z
        result['fly_key'] = b.handle_fly_key() if hasattr(b, 'handle_fly_key') else 'n/a'
        result['flying'] = b.is_flying()
        return step('flying')
    if st['step'] == 'flying' and since > 2.5:
        p, m = me(w)
        result['flight_rose_cm'] = round(p.get_actor_location().z - st['z0'])
        result['still_flying'] = b.is_flying()
        result['narration'] = [l.text for l in b.get_editor_property('chat_history') if l.sender == '*'][-3:]
        return step('shove')
    if st['step'] == 'shove':
        if b.is_flying(): b.handle_fly_key()
        p, m = me(w)
        if m.is_limp() or since > 40:
            result['shoved_limp'] = m.is_limp(); st['p0'] = p.get_actor_location(); return step('grab')
    if st['step'] == 'grab':
        if b.get_dragged_by() == 'drag-bot' or since > 15:
            result['grabbed_by'] = b.get_dragged_by(); return step('dragging')
    if st['step'] == 'dragging' and since > 1.6:
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('release')
    if st['step'] == 'release':
        if (b.get_dragged_by() == '' and since > 1) or since > 10:
            p, m = me(w)
            d = p.get_actor_location() - st['p0']
            result['dragged_cm'] = [round(d.x), round(d.y), round(d.z)]
            result['pins_after_release'] = b.get_pin_count(); result['still_limp'] = m.is_limp()
            return step('markers')
    if st['step'] == 'markers' and since > 2:
        result['pin_markers'] = b.get_pin_marker_count()
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('unpin')
    if st['step'] == 'unpin':
        if b.get_pin_count() == 0 or since > 12:
            result['pins_after_unpin'] = b.get_pin_count(); return step('getup')
    if st['step'] == 'getup' and since > 2:
        b.get_up(); return step('end')
    if st['step'] == 'end' and since > 2:
        p, m = me(w); result['final_limp'] = m.is_limp(); return done(True)


handle = unreal.register_slate_post_tick_callback(tick)
