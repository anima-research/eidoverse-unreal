import unreal,runpy
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
# Restore the recorded pre-test preference; never SaveConfig temporary profiling settings.
settings.set_editor_property('bThrottleCPUWhenNotForeground',True)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
for command in ['r.ProfileGPU.ShowUI 1','r.ProfileGPU.ShowEventHistogram 0']:
 unreal.SystemLibrary.execute_console_command(world,command)
here=Path(__file__).resolve().parent
runpy.run_path(str(here/'test_gpu_field.py'),run_name='__main__')
runpy.run_path(str(here/'audit_maps.py'),run_name='__main__')
unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(unreal.Vector(2050,3900,1100),unreal.Rotator(pitch=-17,yaw=134,roll=0))
unreal.log('SPECTRAL_COMPOSITION_REVIEW_DONE')
