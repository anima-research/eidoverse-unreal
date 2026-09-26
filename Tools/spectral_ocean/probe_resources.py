import unreal
from editor_toolset.toolsets.material import MaterialTools as M
mel=unreal.MaterialEditingLibrary
root='/Game/Underwater/SpectralPrototype'
unreal.EditorAssetLibrary.make_directory(root)
mat=unreal.load_asset(root+'/M_ResourceProbe')
if not mat:mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_ResourceProbe',root,unreal.Material,unreal.MaterialFactoryNew())
for e in M.get_expressions(mat):mel.delete_material_expression(mat,e)
mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
t=mel.create_material_expression(mat,unreal.MaterialExpressionTextureObjectParameter)
t.set_editor_property('parameter_name','Ocean0');t.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
c=mel.create_material_expression(mat,unreal.MaterialExpressionCustom)
i=unreal.CustomInput();i.set_editor_property('input_name','Ocean0');c.set_editor_property('inputs',[i])
c.set_editor_property('code','''struct FTest { Texture2D Field; SamplerState Samp; float3 Read(float2 p) { return Texture2DSampleLevel(Field,Samp,p,0).rgb; } }; FTest f; f.Field=Ocean0; f.Samp=Ocean0Sampler; return f.Read(float2(.5,.5));''')
c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
mel.connect_material_expressions(t,'',c,'Ocean0');mel.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
M.recompile(mat)
unreal.log('SPECTRAL_RESOURCE_PROBE_PASS')
