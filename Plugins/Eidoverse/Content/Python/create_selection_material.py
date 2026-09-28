"""Create /Eidoverse/UI/M_EidoverseSelection: the build-mode selection overlay
(unlit translucent seafoam with a fresnel rim). Idempotent.
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
"""
import traceback
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
MEL = unreal.MaterialEditingLibrary
PATH, NAME = '/Eidoverse/UI', 'M_EidoverseSelection'
try:
    mat = unreal.load_asset(PATH + '/' + NAME)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    color = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -400, 0)
    color.set_editor_property('constant', unreal.LinearColor(0.56 * 2.2, 0.91 * 2.2, 0.78 * 2.2, 1))  # web --brand #8fe8c8, lifted
    fresnel = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -600, 200)
    fresnel.set_editor_property('exponent', 2.5)
    scale = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 200)
    scale.set_editor_property('const_b', 0.75)
    base = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -200, 200)
    base.set_editor_property('const_b', 0.04)
    MEL.connect_material_expressions(fresnel, '', scale, 'A')
    MEL.connect_material_expressions(scale, '', base, 'A')
    MEL.connect_material_property(color, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(base, '', unreal.MaterialProperty.MP_OPACITY)
    errors = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    unreal.log_warning('SELECTION_MATERIAL_READY errors=%s' % (list(errors) if errors else []))
except Exception:
    unreal.log_error('SELECTION_MATERIAL_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
