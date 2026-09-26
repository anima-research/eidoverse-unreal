"""Build spectral materials and a separate, recoverable copy of the demo map."""
import unreal
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
mel=unreal.MaterialEditingLibrary;assets=unreal.EditorAssetLibrary
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
root='/Game/Underwater/SpectralPrototype'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assets.make_directory(root)
def prop(o,n,v):o.set_editor_property(n,v)
def expression(mat,cls,**kw):
    e=mel.create_material_expression(mat,cls)
    for n,v in kw.items():prop(e,n,v)
    return e
def duplicate(src,name):
    dst=root+'/'+name
    return unreal.load_asset(dst) or assets.duplicate_asset(src,dst)
def add_inputs(mat,c):
    inputs=list(c.get_editor_property('inputs'));names={str(i.get_editor_property('input_name')) for i in inputs}
    nodes={}
    for name in ('Ocean0','Ocean1','Ocean2','OceanWake'):
        nodes[name]=expression(mat,unreal.MaterialExpressionTextureObjectParameter,parameter_name=name,texture=unreal.load_asset('/Engine/EngineResources/DefaultTexture'))
    nodes['UnderReflection']=expression(mat,unreal.MaterialExpressionTextureObjectParameter,parameter_name='UnderReflection',texture=unreal.load_asset('/Engine/EngineMaterials/DefaultCubemap'))
    for name,default in [('WakeOrigin',unreal.LinearColor(0,0,6400,0)),('OceanGridCenter',unreal.LinearColor(0,0,0,0))]:
        nodes[name]=expression(mat,unreal.MaterialExpressionVectorParameter,parameter_name=name,default_value=default)
    nodes['CameraDepth']=expression(mat,unreal.MaterialExpressionScalarParameter,parameter_name='CameraDepth',default_value=-1000)
    for name in nodes:
        if name not in names:
            i=unreal.CustomInput();prop(i,'input_name',name);inputs.append(i)
    prop(c,'inputs',inputs)
    for name,node in nodes.items():assert mel.connect_material_expressions(node,'',c,name)
field='''FSpectralField field;
field.Ocean0=Ocean0;field.Ocean1=Ocean1;field.Ocean2=Ocean2;field.OceanWake=OceanWake;
field.S0=Ocean0Sampler;field.S1=Ocean1Sampler;field.S2=Ocean2Sampler;field.SW=OceanWakeSampler;
field.WakeOrigin=float4(WakeOrigin.xyz,1.0);
'''

legacy=unreal.load_asset('/Game/Underwater/Materials/M_OceanSurface')
legacy_custom=next(e for e in M.get_expressions(legacy) if isinstance(e,unreal.MaterialExpressionCustom))
legacy_code=legacy_custom.get_editor_property('code')
air=legacy_code[legacy_code.index('FWaterOptics air;'):legacy_code.index('FOceanWaves waves;')]
body='''
float2 p=WorldPos.xy;
float dist=max(abs(p.x-OceanGridCenter.x),abs(p.y-OceanGridCenter.y));
float cell=max(50.0,50.0+max(dist-6000.0,0.0)*.065);
float4 displaced=field.Displacement(p,cell*.6);
Displacement=displaced.xyz;
float footprint=1.0;
#if PIXELSHADER
footprint=max(1.0,max(length(ddx(p)),length(ddy(p))));
#endif
float3 normal=field.Normal(p,footprint);
float3 atSurface=float3(p+displaced.xy,Surface+displaced.z);
Mask=1.0;
[loop] for(int i=0;i<16;i++) {
    float2 hit;
    if(air.BoxInterval(atSurface,float3(0,0,1),i,hit)&&hit.x<0&&hit.y>0)Mask=0;
}
if(SubC.a>.5) {
    float3 r=atSurface-SubC.xyz,z=normalize(cross(SubX,SubY));
    float3 q=float3(dot(r,SubX),dot(r,SubY),dot(r,z));
    float cap=1;
    if(q.x<-1750)cap=sqrt(saturate(1-pow((q.x+1750)/330,2)));
    if(q.x>-80)cap=sqrt(saturate(1-pow((q.x+80)/330,2)))*.98;
    if(q.x>-2070&&q.x<245&&length(q.yz/float2(278,263))<cap)Mask=0;
}
float foam=field.Displacement(p,footprint).w;
foam=smoothstep(.14,.7,foam)*.7;
Base=lerp(float3(0,0,0),float3(.45,.48,.46),foam);
Roughness=lerp(.15+smoothstep(25,1000,footprint)*.09,.5,foam);
Emissive=0;
Opacity=0.0;
float cosTheta=saturate(abs(dot(normalize(CamPos-atSurface),normal)));
float fresnel=.0204+.9796*pow(1-cosTheta,5);
// Water-to-air critical angle; real scene refraction inside the window, reflective outside.
UnderOpacity=Mask*max(fresnel,1.0-smoothstep(.64,.68,cosTheta));
return normal;
'''
for name,under in [('M_SpectralSurface',False),('M_SpectralUnderside',True)]:
    mat=duplicate('/Game/Underwater/Materials/M_OceanSurface',name)
    custom=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
    add_inputs(mat,custom)
    prop(custom,'include_file_paths',['/Plugin/UnderwaterOptics/SpectralWaterOptics.ush'])
    shader_body=body
    if under:
        prop(mat,'allow_front_layer_translucency',False)
        # The underwater medium must attenuate the already-composited interface.
        # AfterDOF drew this surface on top of the BeforeDOF water optics pass.
        prop(mat,'translucency_pass',unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
        shader_body=shader_body.replace('Emissive=0;','''float3 reflected=reflect(normalize(atSurface-CamPos),normal);
float4 reflectedSample=TextureCubeSampleLevel(UnderReflection,UnderReflectionSampler,reflected,1.0);
float reflectedMetres=min(300.0,max(reflectedSample.a,0.0)*.01/max(max(abs(reflected.x),abs(reflected.y)),abs(reflected.z)));
float3 reflectedT=exp(-float3(.065,.04,.03)*reflectedMetres);
Emissive=reflectedSample.rgb*reflectedT+float3(.005,.026,.041)*(1-reflectedT);''')
    prop(custom,'code',field+air+shader_body)
    outputs=list(custom.get_editor_property('additional_outputs'));names={str(o.get_editor_property('output_name')) for o in outputs}
    for name_out in ('Opacity','UnderOpacity'):
        if name_out not in names:
            o=unreal.CustomOutput();prop(o,'output_name',name_out);prop(o,'output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1);outputs.append(o)
    prop(custom,'additional_outputs',outputs)
    for e in M.get_expressions(mat):
        if isinstance(e,unreal.MaterialExpressionWorldPosition):prop(e,'world_position_shader_offset',unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    prop(mat,'two_sided',True);prop(mat,'tangent_space_normal',False)
    water_model=getattr(unreal.MaterialShadingModel,next(n for n in dir(unreal.MaterialShadingModel) if 'SINGLE' in n and 'WATER' in n))
    prop(mat,'shading_model',unreal.MaterialShadingModel.MSM_UNLIT if under else water_model)
    prop(mat,'blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if under else unreal.BlendMode.BLEND_MASKED)
    if under:
        prop(mat,'translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        prop(mat,'allow_translucent_custom_depth_writes',True)
        prop(mat,'opacity_mask_clip_value',.001)
        prop(mat,'refraction_method',unreal.RefractionMode.RM_INDEX_OF_REFRACTION)
        prop(mat,'screen_space_reflections',False)
        mel.connect_material_property(expression(mat,unreal.MaterialExpressionConstant,r=.75),'',unreal.MaterialProperty.MP_REFRACTION)
    else:
        water=next((e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionSingleLayerWaterMaterialOutput)),None)
        if not water:water=expression(mat,unreal.MaterialExpressionSingleLayerWaterMaterialOutput)
        for n,col in [('ScatteringCoefficients',(.000025,.000075,.0001)),('AbsorptionCoefficients',(.0008,.00025,.00012))]:
            v=expression(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*col,1))
            assert mel.connect_material_expressions(v,'',water,n)
        assert mel.connect_material_expressions(expression(mat,unreal.MaterialExpressionConstant,r=.35),'',water,'PhaseG')
    for out,slot in [('',unreal.MaterialProperty.MP_NORMAL),('Displacement',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET),('Mask',unreal.MaterialProperty.MP_OPACITY_MASK),('Base',unreal.MaterialProperty.MP_BASE_COLOR),('Roughness',unreal.MaterialProperty.MP_ROUGHNESS),('Emissive',unreal.MaterialProperty.MP_EMISSIVE_COLOR),('UnderOpacity' if under else 'Opacity',unreal.MaterialProperty.MP_OPACITY)]:
        assert mel.connect_material_property(custom,out,slot)
    mel.connect_material_property(expression(mat,unreal.MaterialExpressionConstant,r=.255),'',unreal.MaterialProperty.MP_SPECULAR)
    M.recompile(mat);assets.save_loaded_asset(mat)
    unreal.log('SPECTRAL_MATERIAL_READY '+mat.get_name())

for old,name in [('M_WaterOpticsView','M_SpectralOptics'),('M_WaterOpticsLocalLight','M_SpectralLocalLight')]:
    mat=duplicate('/Game/Underwater/Materials/'+old,name)
    c=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
    original=next(e for e in M.get_expressions(unreal.load_asset('/Game/Underwater/Materials/'+old)) if isinstance(e,unreal.MaterialExpressionCustom)).get_editor_property('code')
    add_inputs(mat,c);prop(c,'include_file_paths',['/Plugin/UnderwaterOptics/SpectralWaterOptics.ush'])
    code=original.replace('FWaterOptics w;','FWaterOptics w; w.Spectral=field; w.FullWaterPath=0.0;')
    if name=='M_SpectralOptics':
        # Scene-depth ratios are not a stable world-position reconstruction for
        # sky pixels (infinite/reversed-Z far plane). Convert axial custom depth
        # to ray distance explicitly instead.
        code=code.replace('CamPos+(WorldPos-CamPos)*(CDepth.r/max(SDepth.r,1e-5))','CamPos+normalize(WorldPos-CamPos)*(CDepth.r/max(dot(normalize(WorldPos-CamPos),View.ViewForward),1e-4))')
        # Visible wave geometry already occludes upward air-to-sky rays. Do not
        # re-intersect an unfiltered infinite spectrum behind that raster result.
        code=code.replace('float3 endpoint=WorldPos;', '''
if(OceanEnabled>0.5 && CameraDepth<0.0 && WorldPos.z>Surface+400.0 && WorldPos.z>CamPos.z)
    return SceneColor.rgb;
float3 endpoint=WorldPos;''')
        code=code.replace('return w.ViewRadiance(', '''
// Rasterization supplies the first interface hit. Re-tracing the full FFT
// spectrum per fog pixel is both redundant and inconsistent with mesh LOD.
w.OceanEnabled=0.0;
w.FullWaterPath=(oceanIsFirst && CameraDepth>0.0)?1.0:0.0;
return w.ViewRadiance(''')
    else:
        # Local light transmission retains depth/air-volume attenuation without
        # hundreds of FFT fetches per illuminated pixel at the waterline.
        code=code.replace('w.OceanEnabled=OceanEnabled;','w.OceanEnabled=0.0;')
    prop(c,'code',field+code)
    M.recompile(mat);assets.save_loaded_asset(mat)
    unreal.log('SPECTRAL_MATERIAL_READY '+name)

task=unreal.AssetImportTask()
for n,v in dict(filename=str(Path(__file__).parent/'exports/SM_SpectralOceanGrid.fbx'),destination_path=root,destination_name='SM_SpectralOceanGrid',automated=True,replace_existing=True,save=True).items():prop(task,n,v)
opt=unreal.FbxImportUI()
for n,v in dict(import_mesh=True,import_materials=False,import_textures=False,import_as_skeletal=False,automated_import_should_detect_type=False,mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():prop(opt,n,v)
sm=opt.get_editor_property('static_mesh_import_data')
for n,v in dict(combine_meshes=True,auto_generate_collision=False,generate_lightmap_u_vs=False,convert_scene=True,convert_scene_unit=True).items():prop(sm,n,v)
prop(task,'options',opt);prop(task,'factory',unreal.FbxFactory())
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
mesh=unreal.load_asset(root+'/SM_SpectralOceanGrid')
prop(mesh,'positive_bounds_extension',unreal.Vector(500,500,500));prop(mesh,'negative_bounds_extension',unreal.Vector(500,500,500));assets.save_loaded_asset(mesh)

import runpy
runpy.run_path(str(Path(__file__).with_name('install_map.py')),run_name='__main__')
