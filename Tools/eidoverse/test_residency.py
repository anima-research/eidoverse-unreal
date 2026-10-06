"""Residency, live: a spawned crate loads; 300 m away it returns to its placeholder
(demoted, mesh released); back near it promotes and loads again. RESIDENCY_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD, LIB = 'ws://127.0.0.1:8993/ws', os.environ['RES_WORLD'], 'eidoverse/assets/models/crate_large_red.glb'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('RESIDENCY_RESULT ' + json.dumps(result, default=str))
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
    b = bs[0]; hud = b.get_editor_property('hud')
    sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-residency'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and pawn and since > 4:
        hud.show_frame('build', True); hud.search_library('crate'); hud.build_action('build:spawn:' + LIB)
        return step('spawned')
    if st['step'] == 'spawned':
        s = sc[0]; eid = hud.get_last_spawned_entity(); st['eid'] = eid
        if (eid and s.is_item_model_loaded(eid)) or since > 20:
            result['loaded_near'] = s.is_item_model_loaded(eid)
            hud.show_frame('build', False)
            # Rejoin: the join's geom carries the crate's bbox (no bbox, never demoted).
            b.join_from_door(SERVER, WORLD, 'unreal-residency')
            unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.World.ResidencyScale 0.1')
            return step('rejoin')
    if st['step'] == 'rejoin' and since > 8 and b.get_editor_property('joined'):
        s = sc[0]; eid = st['eid']
        if s.is_item_model_loaded(eid) or since > 25:
            result['why_near'] = s.describe_residency(eid)
            st['home'] = pawn.get_actor_location(); h = st['home']
            pawn.set_actor_location(unreal.Vector(h.x + 6000, h.y, h.z + 50), False, True)
            return step('far')
    if st['step'] == 'far' and since > 3:
        s = sc[0]; eid = st['eid']
        result['far_demoted'] = s.is_item_demoted(eid); result['far_loaded'] = s.is_item_model_loaded(eid)
        result['demoted_count'] = s.get_demoted_count(); result['why_far'] = s.describe_residency(eid)
        h = st['home']; pawn.set_actor_location(unreal.Vector(h.x, h.y, h.z + 50), False, True)
        return step('back')
    if st['step'] == 'back':
        s = sc[0]; eid = st['eid']
        if s.is_item_model_loaded(eid) or since > 20:
            result['back_demoted'] = s.is_item_demoted(eid); result['back_loaded'] = s.is_item_model_loaded(eid)
            result['reload_s'] = round(since, 1)
            return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
