"""Update only the ocean shading; preserve room masks and the shared wave physics.

Run through the editor Python console with L_Epipelagic open, outside PIE.
The shader source is embedded so every iteration invalidates the material cache.
"""
from pathlib import Path
import unreal
from editor_toolset.toolsets.material import MaterialTools as M

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
mel=unreal.MaterialEditingLibrary
mat=unreal.load_asset('/Game/Underwater/Materials/M_OceanSurface')
assert mat
custom=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
old=custom.get_editor_property('code')
# Preserve all air-volume input assignments, and their graph connections.
init=old[old.index('FWaterOptics air;'):old.index('FOceanWaves waves;')]
shader=Path(__file__).with_name('OceanSurfaceDetail.ush').read_text()
body='''
FOceanWaves waves;
FOceanSurfaceDetail detail;
float3 swell=waves.Sample(WorldPos.xy,Time)*Enabled;
float3 atSurface=float3(WorldPos.xy,Surface+swell.x);
float contact=detail.ContactWeight(atSurface,SubC,SubX,SubY)*Enabled;
float hullDistance=detail.HullDistance(atSurface,SubC,SubX,SubY);
Displacement=float3(0,0,detail.RenderHeight(WorldPos.xy,Time)*Enabled);
if(contact>0.0) Displacement.z+=detail.ContactHeight(atSurface,Time,SubC,SubX,SubY)*Enabled;
float unresolved;
float2 gradient=detail.SurfaceGradient(WorldPos.xy,Time,unresolved)*Enabled;
float foam=0.0;
#if PIXELSHADER
if(contact>0.0 && hullDistance<550.0) {
    // Finite-difference the small pressure wave into the normal as well as WPO.
    float eps=12.0;
    float gx=detail.ContactHeight(atSurface+float3(eps,0,0),Time,SubC,SubX,SubY)-detail.ContactHeight(atSurface-float3(eps,0,0),Time,SubC,SubX,SubY);
    float gy=detail.ContactHeight(atSurface+float3(0,eps,0),Time,SubC,SubX,SubY)-detail.ContactHeight(atSurface-float3(0,eps,0),Time,SubC,SubX,SubY);
    gradient+=float2(gx,gy)/(2.0*eps)*Enabled;
    float2 drift=float2(Time*9.0,-Time*7.0);
    float coarse=detail.Noise((WorldPos.xy+drift)/86.0).x;
    float fine=detail.Noise((WorldPos.xy-drift*.7)/16.0).x;
    float footprint=max(length(ddx(WorldPos.xy)),length(ddy(WorldPos.xy)));
    fine=lerp(fine,.5,smoothstep(4.0,16.0,footprint));
    float band=1.0-smoothstep(12.0+coarse*30.0,58.0+coarse*70.0,max(hullDistance,0.0));
    foam=contact*band*smoothstep(.42,.74,coarse*.65+fine*.35)*.68;
}
#endif
float3 normal=normalize(float3(-gradient,1));
Mask=1.0;
[loop] for(int i=0;i<16;i++) {
    float2 hit;
    if(air.BoxInterval(atSurface,float3(0,0,1),i,hit) && hit.x<0 && hit.y>0) Mask=0;
}
if(SubC.a>0.5 && hullDistance<0.0) Mask=0;
Base=lerp(float3(0.008,0.042,0.065),float3(.38,.46,.46),foam);
Roughness=lerp(sqrt(.19*.19+unresolved*2.0),.58,foam);
float3 view=normalize(CamPos-WorldPos);
float below=CamPos.z<Surface+waves.Sample(CamPos.xy,Time).x*Enabled?1.0:0.0;
float cosT=saturate(dot(-view,normal));
float window=smoothstep(0.62,0.74,cosT);
float3 sky=float3(0.30,0.53,0.76)*(0.6+0.4*cosT);
float3 tir=float3(0.018,0.075,0.11);
float sun=pow(saturate(dot(normalize(-view+float3(gradient,0)*0.3),float3(-0.326,-0.228,0.917))),500.0);
Emissive=(lerp(tir,sky,window)+sun*float3(6,5.7,4.8)*window)*below;
return normal;
'''
custom.set_editor_property('code',shader+'\n'+init+body)
outputs=list(custom.get_editor_property('additional_outputs'))
if not any(str(o.get_editor_property('output_name'))=='Roughness' for o in outputs):
    output=unreal.CustomOutput()
    output.set_editor_property('output_name','Roughness')
    output.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    outputs.append(output)
    custom.set_editor_property('additional_outputs',outputs)
assert mel.connect_material_property(custom,'Roughness',unreal.MaterialProperty.MP_ROUGHNESS)
M.recompile(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('OCEAN_SURFACE_UPDATE_SUCCESS')
