"""Build reusable lit templates; downloaded avatar files are never modified.

Run outside PIE. The runtime creates per-avatar instances and supplies the
incoming material parameters/textures; these assets contain no avatar content.
"""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
root='/Game/Underwater/Avatars/Materials'
unreal.EditorAssetLibrary.make_directory(root)
def template(source,name):
 path=root+'/'+name
 if unreal.EditorAssetLibrary.does_asset_exist(path):
  asset=unreal.load_asset(path)
 elif source:
  asset=unreal.EditorAssetLibrary.duplicate_asset(source,path)
 else:
  asset=unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,root,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 assert asset,path
 return asset
base=template('/glTFRuntime/M_glTFRuntimeBase','M_AvatarLit')
base.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
base.set_editor_property('translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
base.set_editor_property('used_with_skeletal_mesh',True)
unreal.MaterialEditingLibrary.recompile_material(base)
unreal.EditorAssetLibrary.save_loaded_asset(base)
for source,name in [
 (None,'MI_AvatarLit_Opaque'),
 ('M_glTFRuntimeTwoSided_Inst','MI_AvatarLit_TwoSided'),
 ('M_glTFRuntimeMasked_Inst','MI_AvatarLit_Masked'),
 ('M_glTFRuntimeTwoSidedMasked_Inst','MI_AvatarLit_TwoSidedMasked'),
 ('M_glTFRuntimeTranslucent_Inst','MI_AvatarLit_Translucent'),
 ('M_glTFRuntimeTwoSidedTranslucent_Inst','MI_AvatarLit_TwoSidedTranslucent'),
 ('M_glTFRuntimeTranslucent_Inst','MI_AvatarPBR_Translucent'),
 ('M_glTFRuntimeTwoSidedTranslucent_Inst','MI_AvatarPBR_TwoSidedTranslucent')]:
 material=template('/glTFRuntime/'+source if source else None,name)
 unreal.MaterialEditingLibrary.set_material_instance_parent(material,base)
 unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material,'metallicFactor',1.0 if 'PBR' in name else 0.0)
 unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(material,'roughnessFactor',1.0 if 'PBR' in name else 0.8)
 unreal.EditorAssetLibrary.save_loaded_asset(material)
unreal.log('EIDOVERSE_LIT_TEMPLATES_READY')
