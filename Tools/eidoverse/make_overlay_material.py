"""M_EidoverseOverlay: unlit, translucent (0.9), no depth test — markers that show through
geometry, like the web's depthTest:false gizmos (seat anchors, pins, structure ghosts). Params: Color, Opacity."""
import unreal
MEL = unreal.MaterialEditingLibrary
path = '/Eidoverse/UI/M_EidoverseOverlay'
m = unreal.load_asset(path)
if not m:
    m = unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseOverlay', '/Eidoverse/UI', unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('disable_depth_test', True)
m.set_editor_property('two_sided', True)
m.set_editor_property('used_with_instanced_static_meshes', True)
MEL.delete_all_material_expressions(m)
c = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -400, 0)
c.set_editor_property('parameter_name', 'Color'); c.set_editor_property('default_value', unreal.LinearColor(0.96, 0.79, 0.42, 1))
o = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -400, 200)
o.set_editor_property('parameter_name', 'Opacity'); o.set_editor_property('default_value', 0.9)
MEL.connect_material_property(c, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.connect_material_property(o, '', unreal.MaterialProperty.MP_OPACITY)
MEL.recompile_material(m)
unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log_warning('OVERLAY_MAT ok')
unreal.SystemLibrary.quit_editor()
