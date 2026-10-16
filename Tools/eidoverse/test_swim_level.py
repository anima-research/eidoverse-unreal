"""Wading and swimming in the world's water level, live (terrain + level 0.5 by the bot; the bot
streams clip 'swim'). Deep water floats me (feet ~ level - 0.75*1.7) treading; the shallows wade
slower; the bot's body takes the stroke layer. SWIMLEVEL_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['SL_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'zs': []}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('SWIMLEVEL_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def wire_to_ue(o, x, y, z): return unreal.Vector(o.x + z * 100, o.y - x * 100, o.z + y * 100)
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 100: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-swimmer'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and isinstance(pawn, unreal.EidoverseExplorer) and since > 12:
        o = b.get_editor_property('origin_centimeters'); st['o'] = o
        pawn.set_actor_location(wire_to_ue(o, 0, 2, -55) + unreal.Vector(0, 0, 90), False, True)
        return step('deep')
    if st['step'] == 'deep':
        o = st['o']
        if since > 4: st['zs'].append((pawn.get_actor_location().z - o.z - 88) / 100)
        if since > 7:
            result['deep'] = {'swimming': pawn.get_editor_property('level_swim'), 'feet_min': round(min(st['zs']), 3), 'feet_max': round(max(st['zs']), 3),
                              'expect_feet': round(0.5 - 0.75 * 1.7, 3)}
            ms = pawn.get_components_by_class(unreal.EidoverseAvatarMesh)
            result['self_swim_weight'] = round(ms[0].get_swim_weight(), 2) if ms else None
            m = b.get_peer_body('swim-bot'); result['bot_swim_weight'] = round(m.get_swim_weight(), 2) if m else None
            pawn.set_actor_location(wire_to_ue(o, -40, 1.5, -40) + unreal.Vector(0, 0, 90), False, True)
            return step('shallow')
    if st['step'] == 'shallow' and since > 3:
        result['shallow'] = {'swimming': pawn.get_editor_property('level_swim'), 'wade_factor': round(pawn.get_editor_property('wade_factor'), 3)}
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
