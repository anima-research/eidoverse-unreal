"""Particles, visual: fire, smoke and magic emitters on three crates. PARTICLES_RESULT + shot."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['FX_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('PARTICLES_RESULT ' + json.dumps(result, default=str))
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
        b.join_from_door(SERVER, WORLD, 'unreal-fx-test'); return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and since > 12): return
        o = b.get_editor_property('origin_centimeters')
        pawn.set_actor_location(unreal.Vector(o.x - 600, o.y, 200), False, True)
        pawn.set_camera_distance(0)
        pc.set_control_rotation(unreal.Rotator(0, -2, 0))
        result['ism'] = sum(len(a.get_components_by_class(unreal.InstancedStaticMeshComponent)) for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor) if a.tags and str(a.tags[0]) in ('fire', 'smoke', 'magic'))
        return step('shot')
    if st['step'] == 'shot' and since > 3:
        unreal.SystemLibrary.execute_console_command(w, 'shot')
        return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
