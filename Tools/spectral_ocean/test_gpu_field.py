"""Compare renderer samples with native physics across signs and periodic seams."""
import unreal,json,math
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ocean=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.SpectralOcean))
ocean.initialize_ocean();fields=ocean.get_editor_property('fields');assert len(fields)==3
root='/Game/Underwater/SpectralPrototype';path=root+'/M_SpectralRegression';mel=unreal.MaterialEditingLibrary
mat=unreal.load_asset(path) or unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_SpectralRegression',root,unreal.Material,unreal.MaterialFactoryNew())
for e in M.get_expressions(mat):mel.delete_material_expression(mat,e)
mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
c=mel.create_material_expression(mat,unreal.MaterialExpressionCustom)
c.set_editor_property('include_file_paths',['/Plugin/UnderwaterOptics/SpectralField.ush'])
c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
nodes={}
for i in range(4):
    name='Ocean'+str(i) if i<3 else 'OceanWake'
    node=mel.create_material_expression(mat,unreal.MaterialExpressionTextureObjectParameter)
    node.set_editor_property('parameter_name',name);node.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
    nodes[name]=node
nodes['UV']=mel.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate)
inputs=[]
for name in nodes:
    inp=unreal.CustomInput();inp.set_editor_property('input_name',name);inputs.append(inp)
c.set_editor_property('inputs',inputs)
for n,node in nodes.items():mel.connect_material_expressions(node,'',c,n)
# Authored analytic waves: the water world's four, in UE cm (wire dir [x,z] -> UE (z,-x)).
AUTHORED=[(38.0,24.0,(0.3420201,0.9396926),0.0),(23.0,13.7,(0.9063078,0.4226183),1.7),(12.0,7.1,(0.6156615,-0.7880108),3.2),(6.0,3.3,(-0.9781476,0.2079117),0.8)]
waves=[unreal.Vector4(a,2*math.pi/(l*100),d[1],-d[0]) for a,l,d,_ in AUTHORED]
phases=[ph+12345.678 for *_,ph in AUTHORED]  # large phase: exercises CPU-side reduction
ocean.set_authored_waves(waves,phases)
reduced=[math.fmod(ph,2*math.pi) for ph in phases]
aw_init=''.join(f'f.AW[{i}]=float4({w.x!r},{w.y!r},{w.z!r},{w.w!r});' for i,w in enumerate(waves))
aw_init+=''.join(f'f.AW[{i}]=float4(0,0,1,0);' for i in range(len(waves),8))
aw_init+='f.AWP0=float4(%r,%r,%r,%r);f.AWP1=0;f.AWN=%d;'%(*reduced,len(waves))
c.set_editor_property('code','''FSpectralField f;
f.Ocean0=Ocean0;f.Ocean1=Ocean1;f.Ocean2=Ocean2;f.OceanWake=OceanWake;
f.S0=Ocean0Sampler;f.S1=Ocean1Sampler;f.S2=Ocean2Sampler;f.SW=OceanWakeSampler;f.WakeOrigin=float4(0,0,6400,0);'''+aw_init+'''
int i=min(int(UV.x*16),15);
float x=(i<8?float(i)*1700.0-6400.0:float(i-8)*12800.0-25600.25);
float2 p=float2(x,321.25+float(i)*43.0);
float3 d=f.Displacement(p,1).xyz;
return float3(f.Height(p)+500,d.z+500,d.x+500);
''')
mel.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);M.recompile(mat)
mid=unreal.MaterialLibrary.create_dynamic_material_instance(world,mat)
for i,t in enumerate(fields):mid.set_texture_parameter_value('Ocean'+str(i),t)
mid.set_texture_parameter_value('OceanWake',ocean.get_editor_property('wake_field'))
expected=[]
for i in range(16):
    p=unreal.Vector(i*1700-6400 if i<8 else (i-8)*12800-25600.25,321.25+i*43,0)
    d=ocean.get_displacement(p);expected.append((ocean.get_surface_height(p)+500,d.z+500,d.x+500))
rt=unreal.RenderingLibrary.create_render_target2d(world,16,1,unreal.TextureRenderTargetFormat.RTF_RGBA32F)
unreal.RenderingLibrary.draw_material_to_render_target(world,rt,mid)
errors=[]
try:
    for i,want in enumerate(expected):
        got=unreal.RenderingLibrary.read_render_target_raw_pixel(world,rt,i,0,False)
        error=max(abs(a-b) for a,b in zip((got.r,got.g,got.b),want))
        assert math.isfinite(error) and error<.5,(i,error,want,str(got))
        errors.append(error)
finally:unreal.RenderingLibrary.release_render_target2d(rt)
authored=[abs(ocean.get_authored_height(unreal.Vector(i*1700-6400 if i<8 else (i-8)*12800-25600.25,321.25+i*43,0))) for i in range(16)]
assert max(authored)>10,('authored waves did not contribute',authored)
ocean.set_authored_waves([],[])
report={'passed':16,'authored_waves':len(waves),'max_authored_cm':max(authored),'max_error_cm':max(errors),'simulation_ms_snapshot':ocean.get_editor_property('simulation_milliseconds')}
Path('/Users/olena/unrealtest/demo_reel/spectral_gpu_validation.json').write_text(json.dumps(report,indent=2))
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('SPECTRAL_GPU_VALIDATION_PASS '+json.dumps(report))
