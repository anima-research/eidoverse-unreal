"""Revoke local fixture sharing and restore editor settings after capture QA."""
import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if w:
    b = unreal.GameplayStatics.get_actor_of_class(w, unreal.EidoverseBridge)
    assert b and b.endpoint == 'ws://127.0.0.1:18998/ws'
    b.set_share_scene_screenshots(False)
    unreal.log('REMOTE_CAPTURE_OPT_OUT: sharing disabled')
try:
    unreal.unregister_slate_post_tick_callback(watch_handle)
except Exception:
    pass
try:
    capture_perf.set_editor_property('bThrottleCPUWhenNotForeground', capture_throttle)
except NameError:
    pass
unreal.SystemLibrary.execute_console_command(w, 'Slate.bAllowThrottling 1')
