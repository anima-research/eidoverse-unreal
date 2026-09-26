import unreal, runpy
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
runpy.run_path('/Users/olena/Documents/Unreal Projects/Test1/Tools/eidoverse/build_world_material.py')
unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings').set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(None,'Slate.bAllowThrottling 0')
runpy.run_path('/Users/olena/Documents/Unreal Projects/Test1/Tools/eidoverse/test_dynamic_world.py')
