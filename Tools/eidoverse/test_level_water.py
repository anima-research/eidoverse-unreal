"""World water level, live: hilly terrain with water at 0.5 m (fixtures by the bot). The sheet must
exist, the sample must read where water is, and a shot shows lakes/sea. LEVELWATER_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['LW_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('LEVELWATER_RESULT ' + json.dumps(result, default=str))
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
        b.join_from_door(SERVER, WORLD, 'unreal-water'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 12:
        for f in ('chat', 'world', 'settings', 'build', 'people', 'emotes'): hud.show_frame(f, False)
        sc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseScene)[0]
        result['wet'] = sc.get_level_water_wet_count()
        result['probes'] = {f'{x},{z}': sc.describe_level_water(unreal.Vector(x, 0, z)) for x, z in ((0, 0), (20, 20), (-40, -40), (50, 0), (0, -55))}
        o = b.get_editor_property('origin_centimeters')
        # wire (x, z) -> UE (z*100, -x*100): stand on the shore at wire (-25, -25), look out to (-55, -55)
        pawn.set_actor_location(unreal.Vector(o.x - 2500, o.y + 2500, o.z + 600), False, True)
        cam_at = unreal.Vector(o.x - 1500, o.y + 1500, o.z + 1800)
        rot = unreal.MathLibrary.find_look_at_rotation(cam_at, unreal.Vector(o.x - 4500, o.y + 4500, o.z))
        pawn.set_photo_view(cam_at, rot); pc.set_control_rotation(rot)
        return step('shot')
    if st['step'] == 'shot' and since > 3:
        unreal.SystemLibrary.execute_console_command(w, 'shot'); return step('end')
    if st['step'] == 'end' and since > 1.5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
