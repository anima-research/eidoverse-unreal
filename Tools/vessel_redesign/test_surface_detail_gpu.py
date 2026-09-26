"""GPU checks: render/physics agreement nearby, far filtering, contact depth gate."""
import math,unreal
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
mel=unreal.MaterialEditingLibrary
path='/Game/Underwater/Tests/M_SurfaceDetailRegression'
mat=unreal.load_asset(path)
if not mat:
    mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_SurfaceDetailRegression','/Game/Underwater/Tests',unreal.Material,unreal.MaterialFactoryNew())
for e in M.get_expressions(mat):mel.delete_material_expression(mat,e)
mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
uv=mel.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate)
custom=mel.create_material_expression(mat,unreal.MaterialExpressionCustom)
custom.set_editor_property('include_file_paths',['/Plugin/UnderwaterOptics/WaterOptics.ush'])
custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
inp=unreal.CustomInput();inp.set_editor_property('input_name','UV');custom.set_editor_property('inputs',[inp])
custom.set_editor_property('code',Path(__file__).with_name('OceanSurfaceDetail.ush').read_text()+'''
int i=min(int(UV.x*16),15);
FOceanSurfaceDetail d;
FOceanWaves w;
float2 p=float2(i<8 ? -1000.0+float(i)*2000.0 : 18000.0+float(i-8)*11000.0,300);
float centreZ[8]={-1800,-500,-285,-260,-240,0,35,500};
float4 centre=float4(0,0,centreZ[i%8],i==13?0:1);
float3 contactPoint=float3(-1000,300,0);
float h=d.RenderHeight(p,1.2)-w.Sample(p,1.2).x;
float c=d.ContactWeight(contactPoint,centre,float3(1,0,0),float3(0,1,0));
float ripple=d.ContactHeight(contactPoint,1.2,centre,float3(1,0,0),float3(0,1,0));
return float3(h+100,c,ripple+20);
''')
mel.connect_material_expressions(uv,'',custom,'UV')
mel.connect_material_property(custom,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
M.recompile(mat)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rt=unreal.RenderingLibrary.create_render_target2d(world,16,1,unreal.TextureRenderTargetFormat.RTF_RGBA32F)
unreal.RenderingLibrary.draw_material_to_render_target(world,rt,mat)
waves=[(38,2400,.9396926,.3420201,0),(23,1370,.4226183,.9063078,1.7),(12,710,-.7880108,.6156615,3.2),(6,330,.2079117,-.9781476,.8)]
def smooth(a,b,x):
    t=max(0,min(1,(x-a)/(b-a)));return t*t*(3-2*t)
try:
    for i in range(16):
        x=-1000+i*2000 if i<8 else 18000+(i-8)*11000
        cell=max(100,100+max(abs(x)-18000,0)*.07)
        expected=0
        for amp,length,dx,dy,phase in waves:
            k=2*math.pi/length
            h=amp*math.sin(k*(x*dx+300*dy)-math.sqrt(980*k)*1.2+phase)
            expected-=smooth(length*.32,length*.60,cell)*h
        z=[-1800,-500,-285,-260,-240,0,35,500][i%8]
        weight=(1-smooth(240,285,abs(z)))*(0 if i==13 else 1)
        pixel=unreal.RenderingLibrary.read_render_target_raw_pixel(world,rt,i,0,False)
        assert abs(pixel.r-100-expected)<.012,(i,pixel.r,expected)
        assert abs(pixel.g-weight)<.002,(i,pixel.g,weight)
        assert 19-1e-3<=pixel.b<=25+1e-3,(i,pixel.b)
        if weight==0:assert abs(pixel.b-20)<.002,(i,pixel.b)
        unreal.log('SURFACE_DETAIL_GPU_PASS '+str(i))
finally:unreal.RenderingLibrary.release_render_target2d(rt)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('SURFACE_DETAIL_GPU_REGRESSION_SUCCESS')
