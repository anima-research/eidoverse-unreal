"""Create a reusable lit, world-space-detail surface for dynamic world geometry."""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
path='/Game/Underwater/Avatars/Materials/M_EidoverseSurface'
lib=unreal.MaterialEditingLibrary
if unreal.EditorAssetLibrary.does_asset_exist(path):
    material=unreal.load_asset(path)
    lib.delete_all_material_expressions(material)
else:
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseSurface','/Game/Underwater/Avatars/Materials',unreal.Material,unreal.MaterialFactoryNew())
material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
def node(cls): return lib.create_material_expression(material,cls)
base=node(unreal.MaterialExpressionVectorParameter)
base.set_editor_property('parameter_name','baseColorFactor')
base.set_editor_property('default_value',unreal.LinearColor(.6,.55,.45,1))
noise=node(unreal.MaterialExpressionNoise)
noise.set_editor_property('scale',.009)
noise.set_editor_property('levels',2)
noise.set_editor_property('output_min',.96)
noise.set_editor_property('output_max',1.04)
position=node(unreal.MaterialExpressionWorldPosition)
lib.connect_material_expressions(position,'',noise,'Position')
multiply=node(unreal.MaterialExpressionMultiply)
lib.connect_material_expressions(base,'',multiply,'A')
lib.connect_material_expressions(noise,'',multiply,'B')
lib.connect_material_property(multiply,'',unreal.MaterialProperty.MP_BASE_COLOR)
rough=node(unreal.MaterialExpressionScalarParameter)
rough.set_editor_property('parameter_name','roughnessFactor')
rough.set_editor_property('default_value',.86)
lib.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
lib.recompile_material(material)
unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log('EIDOVERSE_WORLD_MATERIAL_READY')
