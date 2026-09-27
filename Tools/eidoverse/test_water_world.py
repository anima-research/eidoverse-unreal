"""Live PIE check: an aquatic world realized from components, not from the level.

Start the isolated preview first (eidoverse-worlds, branch unreal/native-client):
    bun tools/water/preview.ts <bundle> 19350
Then run in the editor (or with -ExecutePythonScript). No production joins, no
saved level changes. Logs WATER_WORLD_RESULT <json> and screenshots to
Saved/Screenshots; quits the editor when QUIT_WHEN_DONE is true.
"""
import json
import math
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)

PORT = globals().get('WATER_TEST_PORT', 19350)
QUIT_WHEN_DONE = globals().get('QUIT_WHEN_DONE', True)
MAP = '/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype'

editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    level.editor_request_begin_play()

state = {'phase': 'connect', 't': 0.0, 'samples': [], 'shots': []}
result = {}


def actors(world, cls):
    return list(unreal.GameplayStatics.get_all_actors_of_class(world, cls))


def live(world, cls):
    return [a for a in actors(world, cls) if not a.actor_has_tag('EidoverseSuspended')]


def finish(ok, why=''):
    result.update(ok=ok, why=why, samples=state['samples'][-12:], shots=state['shots'])
    unreal.log_warning('WATER_WORLD_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    if editor.get_game_world():
        level.editor_request_end_play()
    if QUIT_WHEN_DONE:
        unreal.SystemLibrary.quit_editor()


def shot(world, name):
    unreal.SystemLibrary.execute_console_command(world, f'HighResShot 1280x720 filename={name}')
    state['shots'].append(name)


def aim_at(world, pc, explorer, tag):
    for a in actors(world, unreal.Actor):
        if a.actor_has_tag(tag):
            eye = explorer.get_actor_location() + unreal.Vector(0, 0, 72)
            box_origin, _ = a.get_actor_bounds(False)
            rot = unreal.MathLibrary.find_look_at_rotation(eye, box_origin)
            pc.set_control_rotation(rot)
            return {'tag': tag, 'target_cm': [box_origin.x, box_origin.y, box_origin.z],
                    'distance_m': round(unreal.MathLibrary.vector_distance(eye, box_origin) / 100, 1)}
    return {'tag': tag, 'missing': True}


def tick(delta):
    world = editor.get_game_world()
    state['t'] += delta
    if state['t'] > 180:
        return finish(False, 'timeout in phase ' + state['phase'])
    if not world:
        return
    bridges = actors(world, unreal.EidoverseBridge)
    if not bridges:
        return
    b = bridges[0]
    if state['phase'] == 'connect':
        b.disconnect()
        b.set_editor_property('endpoint', f'ws://127.0.0.1:{PORT}/ws')
        b.set_editor_property('world_name', 'water-preview')
        b.set_editor_property('participant_name', 'unreal-water-test')
        b.set_editor_property('join_token', '')
        b.set_editor_property('identity_token', '')
        b.set_editor_property('render_dynamic_world', True)
        b.set_editor_property('avatar_library_base', f'http://127.0.0.1:{PORT}/library/')
        b.connect()
        state['phase'] = 'wait-scene'
        state['since'] = state['t']
        return
    scenes = actors(world, unreal.EidoverseScene)
    explorers = actors(world, unreal.EidoverseExplorer)
    if not scenes or not explorers:
        return
    scene, explorer = scenes[0], explorers[0]
    if state['phase'] == 'wait-scene':
        loaded = scene.get_editor_property('loaded_models')
        if scene.get_editor_property('water_volume_count') >= 1 and loaded >= 5:
            oceans = live(world, unreal.SpectralOcean)
            controllers = live(world, unreal.WaterOpticsController)
            loc = explorer.get_actor_location()
            result.update(
                entities=scene.get_editor_property('entity_count'),
                models_loaded=loaded, models_failed=scene.get_editor_property('failed_models'),
                water_volumes=scene.get_editor_property('water_volume_count'),
                air_boxes=scene.get_editor_property('air_box_count'),
                oceans=len(oceans), controllers=len(controllers),
                optics_air_registered=controllers[0].get_editor_property('air_volume_count') if controllers else None,
                ocean_surface_z=oceans[0].get_editor_property('surface_z') if oceans else None,
                authored_waves=oceans[0].get_editor_property('authored_wave_count') if oceans else None,
                spawn_cm=[loc.x, loc.y, loc.z], diagnostic=scene.get_editor_property('diagnostic'))
            for tag in ('water-terrain', 'water-habitat', 'water-submarine', 'water-platform', 'water-scenery'):
                for a in actors(world, unreal.Actor):
                    if a.actor_has_tag(tag):
                        o, e = a.get_actor_bounds(False)
                        result.setdefault('bounds', {})[tag] = [[round(o.x), round(o.y), round(o.z)], [round(e.x), round(e.y), round(e.z)]]
            start = explorer.get_actor_location()
            hit = unreal.SystemLibrary.line_trace_single(world, start, start - unreal.Vector(0, 0, 30000),
                unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [explorer], unreal.DrawDebugTrace.NONE, True)
            if hit:
                h = hit.to_tuple()
                result['below_spawn'] = {'hit': bool(h[0]), 'distance_cm': round(h[3], 1) if h[0] else None,
                                         'actor': h[9].get_name() if h[0] and h[9] else None,
                                         'tags': [str(t) for t in h[9].tags] if h[0] and h[9] else None}
            def trace(a_, b_):
                hit = unreal.SystemLibrary.line_trace_single(world, a_, b_, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [explorer], unreal.DrawDebugTrace.NONE, True)
                h = hit.to_tuple() if hit else None
                return {'hit': bool(h and h[0]), 'z': round(h[4].z) if h and h[0] else None, 'actor_tags': [str(t) for t in h[9].tags] if h and h[0] and h[9] else None}
            result['trace_up_from_below_spawn'] = trace(start - unreal.Vector(0, 0, 20000), start)
            hb, _ = [x for x in actors(world, unreal.Actor) if x.actor_has_tag('water-habitat')][0].get_actor_bounds(False)
            result['trace_down_onto_habitat'] = trace(hb + unreal.Vector(0, 0, 3000), hb - unreal.Vector(0, 0, 3000))
            result['trace_up_into_habitat'] = trace(hb - unreal.Vector(0, 0, 3000), hb + unreal.Vector(0, 0, 3000))
            pc0 = unreal.GameplayStatics.get_player_controller(world, 0)
            pc0.set_control_rotation(unreal.Rotator(-89, 0, 0))
            shot(world, 'water_world_down')
            state['phase'] = 'swim'
            state['since'] = state['t']
        return
    loc = explorer.get_actor_location()
    mode = explorer.get_editor_property('locomotion_mode')
    state['samples'].append({'t': round(state['t'] - state['since'], 2), 'z': round(loc.z, 1), 'mode': mode})
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    if state['phase'] == 'swim':
        if state['t'] - state['since'] > 3 and 'underwater_shot' not in result:
            result['aim_underwater'] = aim_at(world, pc, explorer, 'water-habitat')
            result['weather_hour'] = scene.get_editor_property('weather_hour')
            shot(world, 'water_world_underwater')
            result['underwater_shot'] = True
            result['swim_mode_at_depth'] = mode
            result['depth_cm'] = loc.z
        if loc.z > -1000 and 'lookup_shot' not in result:
            pc.set_control_rotation(unreal.Rotator(70, 20, 0))
            shot(world, 'water_world_lookup')
            result['lookup_shot'] = loc.z
        if mode == 'surface':
            result['surfaced_after_s'] = round(state['t'] - state['since'], 1)
            state['phase'] = 'surface'
            state['since'] = state['t']
        elif state['t'] - state['since'] > 150:
            return finish(False, 'never surfaced')
        return
    if state['phase'] == 'surface' and state['t'] - state['since'] > 2:
        result['aim_surface'] = aim_at(world, pc, explorer, 'water-platform')
        result['weather'] = {k: scene.get_editor_property(k) for k in ('weather_state', 'weather_hour', 'cloud_cover', 'sun_light')}
        shot(world, 'water_world_surface')
        result['surface_z_cm'] = loc.z
        result['bridge_status'] = b.get_editor_property('connection_status')
        result['world_render_status'] = b.get_editor_property('world_render_status')
        state['phase'] = 'done'
        state['since'] = state['t']
        return
    if state['phase'] == 'done' and state['t'] - state['since'] > 2:
        z = [s['z'] for s in state['samples'] if s['mode'] == 'swim']
        result['rose_while_idle'] = len(z) > 2 and z[-1] > z[0]
        ok = (result.get('water_volumes') == 1 and result.get('oceans') == 1 and result.get('controllers') == 1
              and result.get('air_boxes') == 15 and result.get('optics_air_registered') == 15
              and result.get('swim_mode_at_depth') == 'swim' and result['rose_while_idle'])
        finish(ok, '' if ok else 'expectations not met')


handle = unreal.register_slate_post_tick_callback(tick)
unreal.log_warning('WATER_WORLD_TEST_STARTED')
