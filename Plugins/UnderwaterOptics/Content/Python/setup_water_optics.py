"""Run once in the editor with -ExecutePythonScript; preserves the original water assets.

The runtime implementation is C++; Python is only the editor migration.
"""
import json
import traceback
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
from editor_toolset.toolsets.material import MaterialTools

ROOT = '/Game/Underwater/Materials'
MEL = unreal.MaterialEditingLibrary
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assets = unreal.AssetToolsHelpers.get_asset_tools()

def prop(obj, name, value):
    obj.set_editor_property(name, value)

def struct(cls, **values):
    result = cls()
    for name, value in values.items(): prop(result, name, value)
    return result

def load(name):
    return unreal.load_asset(ROOT + '/' + name)

def material(name, domain):
    result = load(name)
    if not result:
        result = assets.create_asset(name, ROOT, unreal.Material, unreal.MaterialFactoryNew())
    prop(result, 'material_domain', domain)
    for expr in MaterialTools.get_expressions(result):
        MEL.delete_material_expression(result, expr)
    return result

def expression(mat, cls, **values):
    result = MEL.create_material_expression(mat, cls)
    for key, value in values.items():
        prop(result, key, value)
    return result

def common(mat):
    result = {}
    for name, param in [('WP','WaterParams'),('TP','TurbidityProfile'),('Surface','OpticsSurfaceZ')]:
        result[name] = expression(mat, unreal.MaterialExpressionCollectionParameter,
            collection=load('MPC_Water'), parameter_name=param)
    for name in ('OceanTime','OceanEnabled'):
        result[name] = expression(mat, unreal.MaterialExpressionCollectionParameter,
            collection=load('MPC_Water'), parameter_name=name)
    result['LUT'] = expression(mat, unreal.MaterialExpressionTextureObject,
        texture=unreal.load_asset('/Game/Underwater/Textures/T_WaterSpectralLUT'))
    result['Bsc'] = expression(mat, unreal.MaterialExpressionVectorParameter,
        parameter_name='Scatter_PerMetre', default_value=unreal.LinearColor(0.015,0.025,0.04,1))
    for i in range(16):
        for suffix in ('C','E','X','Y'):
            result[f'Air{i}{suffix}'] = expression(mat, unreal.MaterialExpressionCollectionParameter,
                collection=load('MPC_Water'), parameter_name=f'OpticsAir{i}{suffix}')
    return result

def initialization():
    code = 'FWaterOptics w; w.OceanTime=OceanTime; w.OceanEnabled=OceanEnabled; w.WP=WP; w.TP=TP; w.Surface=Surface; w.Bsc=Bsc;\n'
    for i in range(16):
        code += f'w.C[{i}]=Air{i}C; w.E[{i}]=Air{i}E.xyz; w.X[{i}]=Air{i}X.xyz; w.Y[{i}]=Air{i}Y.xyz;\n'
    return code

def finish(mat, inputs, code, scalar=False):
    custom = expression(mat, unreal.MaterialExpressionCustom,
        code=initialization()+code,
        output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1 if scalar else unreal.CustomMaterialOutputType.CMOT_FLOAT3,
        include_file_paths=['/Plugin/UnderwaterOptics/WaterOptics.ush'])
    prop(custom,'inputs',[struct(unreal.CustomInput,input_name=name) for name in inputs])
    for name, node in inputs.items():
        assert MEL.connect_material_expressions(node, '', custom, name), name
    assert MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.layout_material_expressions(mat)
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat

def run():
    collection=load('MPC_Water')
    vectors=list(collection.get_editor_property('vector_parameters'))
    scalars=list(collection.get_editor_property('scalar_parameters'))
    vnames={str(p.get_editor_property('parameter_name')) for p in vectors}
    snames={str(p.get_editor_property('parameter_name')) for p in scalars}
    for i in range(16):
        for suffix in ('C','E','X','Y'):
            name=f'OpticsAir{i}{suffix}'
            if name not in vnames:
                value=unreal.LinearColor(1 if suffix=='X' else 0,1 if suffix=='Y' else 0,0,0)
                vectors.append(struct(unreal.CollectionVectorParameter,parameter_name=name,default_value=value))
    for name in ('OpticsSurfaceZ','OpticsAirCount','OceanTime','OceanEnabled'):
        if name not in snames:
            scalars.append(struct(unreal.CollectionScalarParameter,parameter_name=name,default_value=0))
    prop(collection,'vector_parameters',vectors)
    prop(collection,'scalar_parameters',scalars)
    unreal.EditorAssetLibrary.save_loaded_asset(collection)

    view=material('M_WaterOpticsView',unreal.MaterialDomain.MD_POST_PROCESS)
    prop(view,'blendable_location',unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    prop(view,'blendable_priority',10)
    inputs=common(view)
    inputs['SceneColor']=expression(view,unreal.MaterialExpressionSceneTexture,scene_texture_id=unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    inputs['WorldPos']=expression(view,unreal.MaterialExpressionWorldPosition)
    inputs['CamPos']=expression(view,unreal.MaterialExpressionCameraPositionWS)
    inputs['SDepth']=expression(view,unreal.MaterialExpressionSceneTexture,scene_texture_id=unreal.SceneTextureId.PPI_SCENE_DEPTH)
    inputs['CDepth']=expression(view,unreal.MaterialExpressionSceneTexture,scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_DEPTH)
    inputs['CStencil']=expression(view,unreal.MaterialExpressionSceneTexture,scene_texture_id=unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    inputs['SunDir']=expression(view,unreal.MaterialExpressionVectorParameter,parameter_name='SunDirWater',default_value=unreal.LinearColor(0.326,0.228,-0.917,1))
    inputs['SunE']=expression(view,unreal.MaterialExpressionScalarParameter,parameter_name='SunIrradiance',default_value=3.18310165)
    inputs['Tint']=expression(view,unreal.MaterialExpressionVectorParameter,parameter_name='InscatterTint',default_value=unreal.LinearColor(0.6,0.85,1,1))
    finish(view,inputs,'''
float3 endpoint=WorldPos;
// Only the reserved interface stencil ends the optical ray; unrelated stencils do not.
if(abs(CStencil.r-1.0)<0.1 && CDepth.r>0.0 && CDepth.r<SDepth.r)
    endpoint=CamPos+(WorldPos-CamPos)*(CDepth.r/max(SDepth.r,1e-5));
return w.ViewRadiance(LUT,LUTSampler,SceneColor.rgb,CamPos,endpoint,SunDir,SunE,Tint,View.PreExposure);
''')
    local=material('M_WaterOpticsLocalLight',unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    inputs=common(local)
    inputs['WorldPos']=expression(local,unreal.MaterialExpressionWorldPosition)
    inputs['SourcePosition']=expression(local,unreal.MaterialExpressionVectorParameter,
        parameter_name='SourcePosition',default_value=unreal.LinearColor(0,0,0,1))
    finish(local,inputs,'''
float3 transmission=w.SourceTransmission(LUT,LUTSampler,SourcePosition,WorldPos);
// Legacy light functions are scalar. Apply a luminance transmission to this source alone.
// View-path wavelength filtering remains RGB in the post-process; do not tint all scene lighting.
return saturate(dot(transmission,float3(0.2126,0.7152,0.0722)));
''',scalar=True)

    # Retain the existing spectral surface-light art direction and instance parameters.
    # Replace the fixed two-room mask and duplicated turbidity integral with shared geometry/math.
    sun=load('LF_SunSpectral')
    expressions=MaterialTools.get_expressions(sun)
    custom=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom))
    original_code=custom.get_editor_property('code')
    if 'FWaterOptics w;' not in original_code:
        tail=original_code[original_code.index('float depth ='):]
        start=tail.index('if (Mode > 0.5) {')
        end=tail.index('float disp =',start)
        replacement='''if (Mode > 0.5) {
    bool dry=false;
    [loop] for(int i=0;i<16;i++) {
        float2 h;
        if(w.BoxInterval(WorldPos,float3(0,0,1),i,h) && h.x<0.0 && h.y>0.0) dry=true;
    }
    if(dry) return 0.0;
    return (Mode>1.5) ? tz*0.04 : tz*(1.0-sharp);
}
'''
        tail=tail[:start]+replacement+tail[end:]
        tail=tail.replace('(SurfaceZ - WorldPos.z)','(Surface - WorldPos.z)').replace('FPROF(depth)','w.Integral(depth)')
        tail=tail.replace('(Mode < 0.5) ? 0.75 : 0.25','(Mode < 0.5) ? 0.75 : ((Mode > 1.5) ? 0.0 : 0.25)')
        tail=tail.replace('Texture2DSample(LUT, LUTSampler, float2(saturate(depth / 250.0), 0.5)).rgb','w.BaseTransmission(LUT,LUTSampler,depth)')
        extra=common(sun)
        old_inputs=list(custom.get_editor_property('inputs'))
        old_names={str(i.get_editor_property('input_name')) for i in old_inputs}
        new={k:v for k,v in extra.items() if k not in old_names}
        prop(custom,'inputs',old_inputs+[struct(unreal.CustomInput,input_name=k) for k in new])
        for name,node in new.items():
            assert MEL.connect_material_expressions(node,'',custom,name)
        prop(custom,'include_file_paths',['/Plugin/UnderwaterOptics/WaterOptics.ush'])
        prop(custom,'code',initialization()+tail)
        MEL.recompile_material(sun)
        unreal.EditorAssetLibrary.save_loaded_asset(sun)

    # Reconnect added pins after Custom.code's editor reconstruction.
    for expr in MaterialTools.get_expressions(sun):
        if isinstance(expr,unreal.MaterialExpressionCollectionParameter):
            param=str(expr.get_editor_property('parameter_name'))
            if param=='OpticsSurfaceZ': MEL.connect_material_expressions(expr,'',custom,'Surface')
            elif param.startswith('OpticsAir'):
                MEL.connect_material_expressions(expr,'',custom,param.replace('Optics',''))
        elif isinstance(expr,unreal.MaterialExpressionVectorParameter) and str(expr.get_editor_property('parameter_name'))=='Scatter_PerMetre':
            MEL.connect_material_expressions(expr,'',custom,'Bsc')
    MEL.recompile_material(sun)
    unreal.EditorAssetLibrary.save_loaded_asset(sun)

    actors=EAS.get_all_level_actors()
    controllers=[a for a in actors if isinstance(a,unreal.WaterOpticsController)]
    controller=controllers[0] if controllers else EAS.spawn_actor_from_class(unreal.WaterOpticsController,unreal.Vector())
    controller.set_actor_label('Water Optics Controller')
    prop(controller,'collection',collection)
    prop(controller,'local_light_material',local)
    prop(controller,'surface_z',0.0)
    for actor in actors:
        if isinstance(actor,unreal.ExponentialHeightFog):
            fog=actor.get_component_by_class(unreal.ExponentialHeightFogComponent)
            prop(fog,'enable_volumetric_fog',False)
            prop(fog,'fog_density',0.0)
            fog.set_visibility(False)
        if isinstance(actor,unreal.DirectionalLight):
            light=actor.get_component_by_class(unreal.DirectionalLightComponent)
            prop(light,'volumetric_scattering_intensity',0.0)
            if light.get_editor_property('light_function_material'):
                prop(light,'light_function_fade_distance',10000000.0)
                prop(light,'disabled_brightness',0.0)
        if isinstance(actor,unreal.PostProcessVolume):
            settings=actor.get_editor_property('settings')
            blendables=settings.get_editor_property('weighted_blendables')
            entries=[x for x in blendables.get_editor_property('array') if x.get_editor_property('object') not in [load('PP_Underwater'),view]]
            entries.append(struct(unreal.WeightedBlendable,weight=1.0,object=view))
            prop(blendables,'array',entries)
            prop(settings,'weighted_blendables',blendables)
            for key in ('auto_exposure_method','auto_exposure_min_brightness','auto_exposure_max_brightness','auto_exposure_speed_up','auto_exposure_speed_down'):
                prop(settings,'override_'+key,True)
            prop(settings,'auto_exposure_method',unreal.AutoExposureMethod.AEM_HISTOGRAM)
            prop(settings,'auto_exposure_min_brightness',-3.3)
            prop(settings,'auto_exposure_max_brightness',6.5)
            prop(settings,'auto_exposure_speed_up',1.725)
            prop(settings,'auto_exposure_speed_down',0.8625)
            prop(actor,'settings',settings)
            tags=list(actor.tags)
            if 'WaterOpticsPostProcess' not in tags: tags.append('WaterOpticsPostProcess')
            prop(actor,'tags',tags)
    unreal.EditorAssetLibrary.save_directory('/Game/Underwater',only_if_is_dirty=True,recursive=True)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.log('WATER_OPTICS_SETUP_SUCCESS')

try:
    run()
except Exception:
    unreal.log_error('WATER_OPTICS_SETUP_FAILED\n'+traceback.format_exc())
    raise
