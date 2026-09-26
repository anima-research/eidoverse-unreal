"""Start PIE against snapshots-wire-test.ts --native, never production.
Then run Tools/eidoverse/capture_remote_views.ts from an external Bun process.
"""
import time
import unreal
capture_perf = unreal.load_object(None, '/Script/UnrealEd.Default__EditorPerformanceSettings')
capture_throttle = capture_perf.get_editor_property('bThrottleCPUWhenNotForeground')
capture_perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
unreal.SystemLibrary.execute_console_command(None, 'Slate.bAllowThrottling 0')
capture_started = time.monotonic()
capture_bridge = None
def capture_tick(dt):
    global capture_bridge
    try:
        if time.monotonic() - capture_started > 45:
            raise RuntimeError('native capture setup timed out')
        w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not w:
            return
        if capture_bridge is None:
            capture_bridge = unreal.GameplayStatics.get_actor_of_class(w, unreal.EidoverseBridge)
            assert capture_bridge
            assert not capture_bridge.share_scene_screenshots, 'capture defaults off'
            capture_bridge.endpoint = 'ws://127.0.0.1:18998/ws'
            capture_bridge.world_name = 'snapshot-test'
            capture_bridge.participant_name = 'unreal-capture-fixture'
            capture_bridge.join_token = 'snapshot-test-door'
            capture_bridge.identity_token = ''
            capture_bridge.connect()
        elif capture_bridge.joined and capture_bridge.remote_count == 1:
            capture_bridge.open_chat()
            assert capture_bridge.say('UI_MUST_NOT_BE_CAPTURED')
            capture_bridge.set_share_scene_screenshots(True)
            unreal.unregister_slate_post_tick_callback(capture_handle)
            unreal.log('REMOTE_CAPTURE_NATIVE_READY: joined scratch world; scene sharing enabled; Slate chat visible')
    except Exception:
        unreal.unregister_slate_post_tick_callback(capture_handle)
        raise
capture_handle = unreal.register_slate_post_tick_callback(capture_tick)
if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
