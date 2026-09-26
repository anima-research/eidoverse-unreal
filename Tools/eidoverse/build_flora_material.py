"""Opaque, two-sided, lit foliage; GPU wind, no per-plant ticking/transparency."""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
lib=unreal.MaterialEditingLibrary
path='/Game/Underwater/Avatars/Materials/M_EidoverseFlora'
m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseFlora','/Game/Underwater/Avatars/Materials',unreal.Material,unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property('two_sided',True)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
def node(cls): return lib.create_material_expression(m,cls)
color=node(unreal.MaterialExpressionVertexColor)
assert lib.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
assert lib.connect_material_property(color,'',unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
rough=node(unreal.MaterialExpressionConstant);rough.set_editor_property('r',.8)
assert lib.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
pos=node(unreal.MaterialExpressionWorldPosition)
time=node(unreal.MaterialExpressionTime)
uv=node(unreal.MaterialExpressionTextureCoordinate)
wind=node(unreal.MaterialExpressionCustom)
wind.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
wind.set_editor_property('code','float tip=saturate(UV.y); float w=sin(P.x*.006+P.y*.009+T*1.4)+.35*sin(P.y*.022-T*3.1); return float3(w*5,w*2,0)*tip*tip*UV.x;')
inputs=[]
for name in ['P','T','UV']:
    i=unreal.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
wind.set_editor_property('inputs',inputs)
for n,key in [(pos,'P'),(time,'T'),(uv,'UV')]:assert lib.connect_material_expressions(n,'',wind,key)
assert lib.connect_material_property(wind,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('EIDOVERSE_FLORA_MATERIAL_READY')

path='/Game/Underwater/Avatars/Materials/M_EidoverseClouds'
m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseClouds','/Game/Underwater/Avatars/Materials',unreal.Material,unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property('two_sided',True)
m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
pos=node(unreal.MaterialExpressionWorldPosition);center=node(unreal.MaterialExpressionObjectPositionWS);time=node(unreal.MaterialExpressionTime)
coverage=node(unreal.MaterialExpressionScalarParameter);coverage.set_editor_property('parameter_name','Coverage');coverage.set_editor_property('default_value',.5)
color=node(unreal.MaterialExpressionVectorParameter);color.set_editor_property('parameter_name','CloudColor');color.set_editor_property('default_value',unreal.LinearColor(.5,.55,.6,1))
assert lib.connect_material_property(color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
c=node(unreal.MaterialExpressionCustom);c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
c.set_editor_property('code','''float3 d=normalize(P-Center); float2 q=d.xy/(max(d.z,0)+.25)*2+float2(T*.004,T*.002); float n=0,a=.57;
for(int j=0;j<4;j++){float2 i=floor(q),f=frac(q);f=f*f*(3-2*f);float4 h=frac(sin(float4(dot(i,float2(127.1,311.7)),dot(i+float2(1,0),float2(127.1,311.7)),dot(i+float2(0,1),float2(127.1,311.7)),dot(i+1,float2(127.1,311.7))))*43758.5453);n+=a*lerp(lerp(h.x,h.y,f.x),lerp(h.z,h.w,f.x),f.y);q=q*2.07+7.3;a*=.48;}
return saturate((n-lerp(.85,.2,K))*5)*smoothstep(.015,.18,d.z);''')
inputs=[]
for name in ['P','Center','T','K']:
    i=unreal.CustomInput();i.set_editor_property('input_name',name);inputs.append(i)
c.set_editor_property('inputs',inputs)
for n,key in [(pos,'P'),(center,'Center'),(time,'T'),(coverage,'K')]:assert lib.connect_material_expressions(n,'',c,key)
assert lib.connect_material_property(c,'',unreal.MaterialProperty.MP_OPACITY)
lib.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('EIDOVERSE_CLOUD_MATERIAL_READY')
