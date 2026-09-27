"""Import Resources/UI/Icons/*.png as UI textures under /Eidoverse/UI/Icons.

Idempotent (replaces existing). Run with the editor:
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
Regenerate the PNGs first with Tools/eidoverse/build_ui_icons.ts.
"""
import os
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
DEST = '/Eidoverse/UI/Icons'
try:
    plugin = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir() + 'Eidoverse')
    src = os.path.join(plugin, 'Resources', 'UI', 'Icons')
    files = sorted(f for f in os.listdir(src) if f.endswith('.png'))
    tasks = []
    for f in files:
        t = unreal.AssetImportTask()
        t.filename = os.path.join(src, f)
        t.destination_path = DEST
        t.replace_existing = True
        t.automated = True
        t.save = False
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for f in files:
        path = DEST + '/' + f[:-4]
        tex = unreal.load_asset(path)
        tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property('srgb', True)
        unreal.EditorAssetLibrary.save_loaded_asset(tex)
    unreal.log_warning('UI_ICONS_IMPORTED %d' % len(files))
except Exception:
    unreal.log_error('UI_ICONS_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
