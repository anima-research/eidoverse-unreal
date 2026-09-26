"""Lit non-tiling soil/turf foundation, separate from building surfaces."""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
lib=unreal.MaterialEditingLibrary
path='/Game/Underwater/Avatars/Materials/M_EidoverseTerrain'
m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseTerrain','/Game/Underwater/Avatars/Materials',unreal.Material,unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
def node(cls):return lib.create_material_expression(m,cls)
pos=node(unreal.MaterialExpressionWorldPosition)
base=node(unreal.MaterialExpressionVectorParameter)
base.set_editor_property('parameter_name','baseColorFactor')
base.set_editor_property('default_value',unreal.LinearColor(.068,.11,.033,1))
c=node(unreal.MaterialExpressionCustom);c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
c.set_editor_property('code','''struct Field {
float noise(float2 p) { float2 i=floor(p),f=frac(p); f=f*f*(3-2*f);
float4 h=frac(sin(float4(dot(i,float2(127.1,311.7)),dot(i+float2(1,0),float2(127.1,311.7)),dot(i+float2(0,1),float2(127.1,311.7)),dot(i+1,float2(127.1,311.7))))*43758.5453);
return lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y); }
}; Field n;
float macro=n.noise(P.xy*.0017), patch=n.noise(P.xy*.012+13.7);
float grain=n.noise(P.xy*.16+41.3);
float fineFade=saturate(1-length(fwidth(P.xy))*.12);
float tone=.70+.38*macro+.18*patch+(grain-.5)*.13*fineFade;
float dry=smoothstep(.48,.85,macro*.65+patch*.35)*.28;
return lerp(Base,Base*float3(1.24,.99,.73),dry)*tone;''')
inputs=[]
for name in ['P','Base']:
    i=unreal.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
c.set_editor_property('inputs',inputs)
assert lib.connect_material_expressions(pos,'',c,'P')
assert lib.connect_material_expressions(base,'',c,'Base')
assert lib.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
rough=node(unreal.MaterialExpressionScalarParameter)
rough.set_editor_property('parameter_name','roughnessFactor');rough.set_editor_property('default_value',.9)
assert lib.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
spec=node(unreal.MaterialExpressionConstant);spec.set_editor_property('r',.22)
assert lib.connect_material_property(spec,'',unreal.MaterialProperty.MP_SPECULAR)
lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('EIDOVERSE_TERRAIN_MATERIAL_READY')
