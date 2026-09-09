"""GPU regression test: renders the actual shared HLSL into a float target.
Run in the editor console: py "<plugin>/Content/Python/test_water_optics.py"
No level changes. A reusable diagnostic material is stored under /Game/Underwater/Tests.
"""
import math
import unreal
from editor_toolset.toolsets.material import MaterialTools

mel=unreal.MaterialEditingLibrary
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(),'Stop PIE before GPU material tests.'
path='/Game/Underwater/Tests'
mat=unreal.load_asset(path+'/M_WaterOpticsRegression')
if not mat:
    mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_WaterOpticsRegression',path,unreal.Material,unreal.MaterialFactoryNew())
for e in MaterialTools.get_expressions(mat): mel.delete_material_expression(mat,e)
mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
uv=mel.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate)
custom=mel.create_material_expression(mat,unreal.MaterialExpressionCustom)
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
custom.set_editor_property('include_file_paths',['/Plugin/UnderwaterOptics/WaterOptics.ush'])
pin=unreal.CustomInput(); pin.set_editor_property('input_name','UV')
custom.set_editor_property('inputs',[pin])
custom.set_editor_property('code','''
FWaterOptics w; w.OceanEnabled=0; w.OceanTime=0; w.Surface=0; w.WP=float4(.12,1.5,.7,.35); w.TP=float4(.15,.35,.4,90); w.Bsc=float3(.015,.025,.04);
[unroll] for(int i=0;i<16;i++) {w.C[i]=0;w.E[i]=float3(1,1,1);w.X[i]=float3(1,0,0);w.Y[i]=float3(0,1,0);}
float3 a=float3(0,0,-100),b=float3(1000,0,-100);
int test=min(int(UV.x*16),15);
if(test==1){w.C[0]=float4(500,0,-100,1);w.E[0]=float3(600,200,200);}
if(test==2){w.C[0]=w.C[1]=float4(500,0,-100,1);w.E[0]=float3(300,200,200);w.E[1]=float3(100,200,200);}
if(test==3){w.C[0]=float4(350,0,-100,1);w.C[1]=float4(600,0,-100,1);w.E[0]=float3(150,200,200);w.E[1]=float3(200,200,200);}
if(test==4){w.C[0]=float4(300,0,-100,1);w.C[1]=float4(700,0,-100,1);w.E[0]=w.E[1]=float3(100,200,200);}
if(test==5){a=float3(0,0,100);b=float3(0,0,-100);}
if(test==6){a=float3(0,0,-100);b=float3(0,0,100);}
if(test==7){a.z=b.z=100;}
if(test==8){w.C[0]=float4(500,0,-100,1);w.E[0]=float3(200,100,100);w.X[0]=float3(.70710678,.70710678,0);w.Y[0]=float3(-.70710678,.70710678,0);}
if(test==9){w.C[0]=float4(500,0,-100,1);w.E[0]=float3(400,200,100);}
if(test==10){b=a;}
if(test==11){a.y=b.y=300;w.C[0]=float4(500,0,-100,1);w.E[0]=float3(300,200,200);}
if(test==12){a=float3(0,0,0);b=float3(0,0,-100);}
if(test==13){[unroll] for(int i=0;i<16;i++){w.C[i]=float4(400,0,-100,1);w.E[i]=float3(200,200,200);}}
if(test==15){w.C[0]=float4(300,0,-100,1);w.C[1]=float4(500,0,-100,1);w.E[0]=w.E[1]=float3(100,200,200);}
float2 water[40];float3 dir;int n=w.WaterSegments(a,b,water,dir);float sum=0;
[loop] for(int i=0;i<n;i++)sum+=water[i].y-water[i].x;
return float3(sum,w.Integral(250),w.Profile(250));
''')
assert mel.connect_material_expressions(uv,'',custom,'UV')
assert mel.connect_material_property(custom,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rt=unreal.RenderingLibrary.create_render_target2d(world,16,1,unreal.TextureRenderTargetFormat.RTF_RGBA32F)
unreal.RenderingLibrary.draw_material_to_render_target(world,rt,mat)
expected=[10,0,4,4,6,1,1,0,10-2*math.sqrt(2),2,0,10,1,6,10,6]
integral=.12*250+6*.15*(1-math.exp(-250/6))+.35*15*math.sqrt(math.pi/2)*(math.erf((250-90)/(15*math.sqrt(2)))+math.erf(90/(15*math.sqrt(2))))+.4*(8*(1-math.exp(-25))+50)
failures=[]
for i,value in enumerate(expected):
    pixel=unreal.RenderingLibrary.read_render_target_raw_pixel(world,rt,i,0,False)
    if abs(pixel.r-value)>=0.002: failures.append((i,'water length',pixel.r,value))
    if abs(pixel.g-integral)>=0.002: failures.append((i,'deep integral',pixel.g,integral))
    if abs(pixel.b-.52)>=0.002: failures.append((i,'deep profile',pixel.b,.52))
    unreal.log(f'OPTICS_GPU_TEST_RESULT case={i} water_metres={pixel.r:.5f} expected={value:.5f}')
unreal.RenderingLibrary.release_render_target2d(rt)
assert not failures,failures
unreal.log('OPTICS_GPU_TESTS_SUCCESS: 16 path cases and deep-profile integral checked on GPU')

# Check the real HDR lookup, including distances past its authored 250 m range.
lut=mel.create_material_expression(mat,unreal.MaterialExpressionTextureObject)
lut.set_editor_property('texture',unreal.load_asset('/Game/Underwater/Textures/T_WaterSpectralLUT'))
pins=list(custom.get_editor_property('inputs'))
pin=unreal.CustomInput(); pin.set_editor_property('input_name','LUT'); pins.append(pin)
custom.set_editor_property('inputs',pins)
custom.set_editor_property('code','''
FWaterOptics w;
float distances[8]={0,1,10,100,249,250,300,500};
return w.BaseTransmission(LUT,LUTSampler,distances[min(int(UV.x*8),7)]);
''')
assert mel.connect_material_expressions(uv,'',custom,'UV')
assert mel.connect_material_expressions(lut,'',custom,'LUT')
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
rt=unreal.RenderingLibrary.create_render_target2d(world,8,1,unreal.TextureRenderTargetFormat.RTF_RGBA32F)
unreal.RenderingLibrary.draw_material_to_render_target(world,rt,mat)
samples=[unreal.RenderingLibrary.read_render_target_raw_pixel(world,rt,i,0,False) for i in range(8)]
for channel in ('r','g','b'):
    values=[getattr(p,channel) for p in samples]
    assert abs(values[0]-1)<.00001,values
    assert all(math.isfinite(v) and 0<=v<=1.00001 for v in values),values
    assert all(b<=a+.00001 for a,b in zip(values,values[1:])),values
assert samples[6].b<samples[5].b and samples[7].b<samples[6].b,samples
unreal.RenderingLibrary.release_render_target2d(rt)
unreal.log('OPTICS_LUT_TESTS_SUCCESS: identity, monotonic RGB, continuing blue attenuation at 300/500 m')
