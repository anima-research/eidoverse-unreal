"""Local-only monitor while external HTTP capture cases run. No game edits."""
import unreal
watch_world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
watch_bridge = unreal.GameplayStatics.get_actor_of_class(watch_world, unreal.EidoverseBridge)
assert watch_bridge.endpoint == 'ws://127.0.0.1:18998/ws'
watch_ocean = unreal.GameplayStatics.get_actor_of_class(watch_world, unreal.SpectralOcean)
watch_camera = unreal.GameplayStatics.get_player_camera_manager(watch_world, 0)
watch_position = watch_camera.get_camera_location()
watch_rotation = watch_camera.get_camera_rotation()
watch_status = ''
watch_checks = 0
def watch_tick(dt):
    global watch_status, watch_checks
    try:
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(watch_handle)
            unreal.log('REMOTE_CAPTURE_WATCH_STOPPED: play session ended')
            return
        current = watch_camera.get_camera_location()
        rot = watch_camera.get_camera_rotation()
        ocean_pos = watch_ocean.get_actor_location()
        assert abs(current.x-watch_position.x) < 100 and abs(current.y-watch_position.y) < 100, 'local camera jumped'
        assert abs(rot.yaw-watch_rotation.yaw) < .1 and abs(rot.pitch-watch_rotation.pitch) < .1, 'local camera rotated'
        assert abs(ocean_pos.x-current.x) < 100 and abs(ocean_pos.y-current.y) < 100, 'ocean did not return to local eye'
        expected_depth = watch_ocean.get_surface_height(current) - current.z
        assert abs(watch_ocean.camera_depth - expected_depth) < 20, 'ocean depth did not return to local eye'
        if abs(expected_depth) > 20:
            assert watch_ocean.surface_mesh.is_visible() == (expected_depth <= 0), 'wrong ocean side after capture'
            assert watch_ocean.under_mesh.is_visible() == (expected_depth > 0), 'wrong underwater side after capture'
        status = watch_bridge.capture_status
        if status.startswith('Shared') and status != watch_status:
            watch_status = status
            watch_checks += 1
            unreal.log('REMOTE_CAPTURE_STATE_PASS ' + status)
    except Exception:
        unreal.unregister_slate_post_tick_callback(watch_handle)
        raise
watch_handle = unreal.register_slate_post_tick_callback(watch_tick)
