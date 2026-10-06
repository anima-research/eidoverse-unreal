"""Create /Eidoverse/UI/M_EidoverseUnlit: opaque unlit colour (VectorParameter Color),
for gizmos the browser draws unlit and untoned (light bulbs, seat anchors, pins). Idempotent.
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
"""
import traceback
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
MEL = unreal.MaterialEditingLibrary
PATH, NAME = '/Eidoverse/UI', 'M_EidoverseUnlit'
try:
    mat = unreal.load_asset(PATH + '/' + NAME)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property('parameter_name', 'Color')
    color.set_editor_property('default_value', unreal.LinearColor(1, 0.85, 0.63, 1))
    MEL.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log_warning('UNLIT_MATERIAL_READY errors=%s' % (list(errors) if errors else []))
except Exception:
    unreal.log_error('UNLIT_MATERIAL_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
