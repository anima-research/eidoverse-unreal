import unreal,runpy,os
from pathlib import Path
profile_script=str(Path(__file__).with_name('profile_live.py'))
profile_tag=os.environ.get('SPECTRAL_PROFILE_TAG','before')
def start(dt):
 if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():return
 unreal.unregister_slate_post_tick_callback(start_profile_handle)
 runpy.run_path(profile_script,run_name='__main__',init_globals={'profile_run_tag':profile_tag})
start_profile_handle=unreal.register_slate_post_tick_callback(start)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
