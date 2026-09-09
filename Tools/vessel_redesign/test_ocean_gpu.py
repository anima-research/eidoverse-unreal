"""Render shared ocean math into float pixels; verify vertical crossings both ways."""
import unreal,math
from editor_toolset.toolsets.material import MaterialTools as M
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
mel=unreal.MaterialEditingLibrary;path='/Game/Underwater/Tests'
m=unreal.load_asset(path+'/M_OceanRegression')
if not m:m=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_OceanRegression',path,unreal.Material,unreal.MaterialFactoryNew())
for e in M.get_expressions(m):mel.delete_material_expression(m,e)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
uv=mel.create_material_expression(m,unreal.MaterialExpressionTextureCoordinate)
c=mel.create_material_expression(m,unreal.MaterialExpressionCustom)
c.set_editor_property('include_file_paths',['/Plugin/UnderwaterOptics/WaterOptics.ush'])
c.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
p=unreal.CustomInput();p.set_editor_property('input_name','UV');c.set_editor_property('inputs',[p])
c.set_editor_property('code','''
int test=min(int(UV.x*16),15);
float2 xy=float2((test%4)*1700.0,321.0);
FWaterOptics w; w.Surface=0; w.OceanTime=1.2; w.OceanEnabled=1;
[unroll] for(int i=0;i<16;i++){w.C[i]=0;w.E[i]=1;w.X[i]=float3(1,0,0);w.Y[i]=float3(0,1,0);}
float3 a=float3(xy,200),b=float3(xy,-200);
if(test>=4 && test<8){a.z=-200;b.z=200;}
if(test>=8 && test<12){a.z=150;b=float3(xy+float2(500,0),150);}
if(test>=12){a.z=-150;b=float3(xy+float2(500,0),-150);}
float2 water[40];float3 direction;int count=w.WaterSegments(a,b,water,direction);float sum=0;
[loop] for(int i=0;i<count;i++)sum+=water[i].y-water[i].x;
FOceanWaves waves;float3 sample=waves.Sample(xy,1.2);
return float3(sum,sample.x+100,sample.y+1);
''')
mel.connect_material_expressions(uv,'',c,'UV');mel.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rt=unreal.RenderingLibrary.create_render_target2d(w,16,1,unreal.TextureRenderTargetFormat.RTF_RGBA32F)
unreal.RenderingLibrary.draw_material_to_render_target(w,rt,m)
waves=[(38,2400,.9396926,.3420201,0),(23,1370,.4226183,.9063078,1.7),(12,710,-.7880108,.6156615,3.2),(6,330,.2079117,-.9781476,.8)]
try:
 for i in range(16):
  x=(i%4)*1700.;height=0.;dx=0.
  for amp,length,vx,vy,phase in waves:
   k=2*math.pi/length;theta=k*(x*vx+321*vy)-math.sqrt(980*k)*1.2+phase
   height+=amp*math.sin(theta);dx+=amp*k*math.cos(theta)*vx
  expected=2+height*.01 if i<8 else (0 if i<12 else 5)
  pixel=unreal.RenderingLibrary.read_render_target_raw_pixel(w,rt,i,0,False)
  assert abs(pixel.r-expected)<.003,(i,pixel.r,expected)
  assert abs(pixel.g-(height+100))<.002,(i,pixel.g,height+100)
  assert abs(pixel.b-(dx+1))<.001,(i,pixel.b,dx+1)
  unreal.log('OCEAN_GPU_PASS '+str(i)+' water metres='+str(pixel.r))
finally:unreal.RenderingLibrary.release_render_target2d(rt)
unreal.log('OCEAN_GPU_REGRESSION_SUCCESS')
