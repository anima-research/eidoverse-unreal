"""Create /Eidoverse/Maps/L_EidoverseClient: an empty level whose World Settings use
AEidoverseGameMode (front door offline, server-authored worlds only). Idempotent.

  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
"""
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
PATH = '/Eidoverse/Maps/L_EidoverseClient'
try:
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(PATH):
        les.load_level(PATH)
    else:
        les.new_level(PATH)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    settings.set_editor_property('default_game_mode', unreal.EidoverseGameMode.static_class())
    les.save_current_level()
    unreal.log_warning('CLIENT_MAP_READY ' + world.get_path_name() + ' mode=' + str(settings.get_editor_property('default_game_mode').get_name()))
except Exception:
    unreal.log_error('CLIENT_MAP_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
