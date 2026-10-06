"""Build tools, live: place a light from the build frame, brighten and lock it (the
inspector rows), and watch another builder's live drag move a crate. TOOLS_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['TOOLS_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('TOOLS_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def light_of(w, b, eid):
    sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)[-1]
    a = sc.entity_actor(eid) if hasattr(sc, 'entity_actor') else None
    for act in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
        if act.tags and str(act.tags[0]) == eid:
            ls = act.get_components_by_class(unreal.PointLightComponent)
            return (round(ls[0].intensity, 2) if ls else None, act)
    return (None, None)
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
        b.join_from_door(SERVER, WORLD, 'unreal-tools-test'); return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and since > 10): return
        hud.show_frame('build', True)
        pc.set_control_rotation(unreal.Rotator(0, -30, 0))
        hud.build_action('build:hold:@light')
        return step('aim')
    if st['step'] == 'aim' and since > 1:
        result['placed'] = hud.place_ghost()
        return step('placed')
    if st['step'] == 'placed' and since > 2:
        st['lamp'] = hud.get_last_spawned_entity()
        result['lamp_before'] = light_of(w, b, st['lamp'])[0]
        hud.select_entity(st['lamp'])
        hud.build_action('tool:light:brighter')
        return step('brighter')
    if st['step'] == 'brighter' and since > 2:
        hud.select_entity(st['lamp'])
        result['lamp_after'] = light_of(w, b, st['lamp'])[0]
        hud.build_action('tool:lock')
        return step('locked')
    if st['step'] == 'locked' and since > 2:
        sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)[-1]
        _, crate = light_of(w, b, 'dragme')
        st['c0'] = crate.get_actor_location() if crate else None
        return step('drag')
    if st['step'] == 'drag' and since > 1:
        _, crate = light_of(w, b, 'dragme')
        c1 = crate.get_actor_location() if crate else None
        result['drag_moves'] = bool(st['c0'] and c1 and (c1 - st['c0']).length() > 20)
        result['narration'] = [l.text for l in b.get_editor_property('chat_history') if l.sender == '*'][-4:]
        return step('end')
    if st['step'] == 'end' and since > .5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
