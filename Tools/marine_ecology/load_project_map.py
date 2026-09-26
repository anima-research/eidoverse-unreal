import unreal
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert level.load_level('/Game/Underwater/Maps/L_Epipelagic')
unreal.log('ECOLOGY_MAP_READY '+unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name())
