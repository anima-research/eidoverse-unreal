import unreal,runpy
from pathlib import Path
nanite_profile_path=str(Path(__file__).with_name('profile_nanite_shadows.py'))
def begin(dt):
 if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():return
 unreal.unregister_slate_post_tick_callback(start_handle)
 runpy.run_path(nanite_profile_path,run_name='__main__')
start_handle=unreal.register_slate_post_tick_callback(begin)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
