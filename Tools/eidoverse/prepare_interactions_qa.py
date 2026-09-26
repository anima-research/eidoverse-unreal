import unreal, runpy
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
root='/Users/olena/Documents/Unreal Projects/Test1/Tools/eidoverse/'
runpy.run_path(root+'build_flora_material.py')
unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings').set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(None,'Slate.bAllowThrottling 0')
runpy.run_path(root+'test_dynamic_world.py',init_globals={'LOAD_TEST_AVATARS':True})
