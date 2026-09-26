import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
for path in unreal.EditorAssetLibrary.list_assets('/Game/Underwater/Ecology',recursive=True,include_folder=False):
 asset=unreal.load_asset(path)
 if asset:unreal.EditorAssetLibrary.save_loaded_asset(asset,only_if_is_dirty=True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('EXPANSION_SAVED')
