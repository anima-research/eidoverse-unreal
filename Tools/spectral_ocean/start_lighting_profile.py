import unreal,runpy
from pathlib import Path
lighting_profile_path=str(Path(__file__).with_name('profile_lighting.py'))
def begin_lighting_profile(dt):
 if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():return
 unreal.unregister_slate_post_tick_callback(lighting_start_handle)
 runpy.run_path(lighting_profile_path,run_name='__main__')
lighting_start_handle=unreal.register_slate_post_tick_callback(begin_lighting_profile)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
