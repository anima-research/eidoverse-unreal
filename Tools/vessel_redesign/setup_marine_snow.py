"""Soft, lit camera-recycled marine particles masked out of all dry volumes."""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
MEL=unreal.MaterialEditingLibrary;AT=unreal.AssetToolsHelpers.get_asset_tools()
ROOT='/Game/Underwater/FX'
def prop(o,n,v):o.set_editor_property(n,v)
def struct(cls,**kw):
 o=cls()
 for k,v in kw.items():prop(o,k,v)
 return o
def expr(mat,cls,**kw):
 o=MEL.create_material_expression(mat,cls)
 for k,v in kw.items():prop(o,k,v)
 return o
mat=unreal.load_asset(ROOT+'/M_MarineSnow')
if not mat:mat=AT.create_asset('M_MarineSnow',ROOT,unreal.Material,unreal.MaterialFactoryNew())
else:
 for e in M.get_expressions(mat):MEL.delete_material_expression(mat,e)
prop(mat,'blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
prop(mat,'two_sided',True)
prop(mat,'translucency_lighting_mode',unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
prop(mat,'used_with_niagara_sprites',True)
prop(mat,'enable_responsive_aa',True)
prop(mat,'output_translucent_velocity',False)
# Composite after temporal reconstruction so subpixel snow does not need static
# history to become visible. DepthFade still occludes it against scene geometry.
prop(mat,'translucency_pass',unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)
collection=unreal.load_asset('/Game/Underwater/Materials/MPC_Water')
inputs={'WorldPos':expr(mat,unreal.MaterialExpressionWorldPosition),'CamPos':expr(mat,unreal.MaterialExpressionCameraPositionWS),'UV':expr(mat,unreal.MaterialExpressionTextureCoordinate)}
for name,param in [('Time','OceanTime'),('Enabled','OceanEnabled'),('Surface','OpticsSurfaceZ')]:
 inputs[name]=expr(mat,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name=param)
init='FWaterOptics air;\n'
for i in range(16):
 for suffix in ('C','E','X','Y'):
  name=f'Air{i}{suffix}';inputs[name]=expr(mat,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name='Optics'+name)
  init+=f'air.{suffix}[{i}]={name}'+(';' if suffix=='C' else '.xyz;')+'\n'
code=init+'''
FOceanWaves waves;
float water=saturate((Surface+waves.Sample(WorldPos.xy,Time).x*Enabled-WorldPos.z)/15.0);
[loop] for(int i=0;i<16;i++) {
 float2 hit;
 if(air.BoxInterval(WorldPos,float3(0,0,1),i,hit) && hit.x<0 && hit.y>0) water=0;
}
float d=length(CamPos-WorldPos);
float r=length((UV-0.5)*2.0);
float soft=pow(saturate(1.0-r*r),2.0);
return water*soft*smoothstep(30.0,100.0,d)*(1.0-smoothstep(1250.0,1800.0,d))*0.38;
'''
custom=expr(mat,unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,include_file_paths=['/Plugin/UnderwaterOptics/WaterOptics.ush'])
prop(custom,'inputs',[struct(unreal.CustomInput,input_name=n) for n in inputs])
for n,e in inputs.items():assert MEL.connect_material_expressions(e,'',custom,n)
fade=expr(mat,unreal.MaterialExpressionDepthFade,fade_distance_default=12.)
assert MEL.connect_material_expressions(custom,'',fade,'Opacity')
MEL.connect_material_property(fade,'',unreal.MaterialProperty.MP_OPACITY)
base=expr(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.44,.49,.46,1))
MEL.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
ambient=expr(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.012,.022,.023,1))
MEL.connect_material_property(ambient,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.layout_material_expressions(mat);MEL.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
system=unreal.load_asset(ROOT+'/NS_MarineSnow')
# Single camera-aware system; bounds cover the current ocean play area. The
# renderer only draws the 18 m view-recycled cloud, not this entire bounds box.
prop(system,'fixed_bounds',unreal.Box(unreal.Vector(-1000000,-1000000,-100000),unreal.Vector(1000000,1000000,100000)))
unreal.EditorAssetLibrary.save_loaded_asset(system)
actor=next((a for a in EAS.get_all_level_actors() if a.get_actor_label()=='Marine Snow | Suspended Particulates'),None)
if not actor:actor=EAS.spawn_actor_from_class(unreal.NiagaraActor,unreal.Vector())
actor.set_actor_label('Marine Snow | Suspended Particulates')
c=actor.get_component_by_class(unreal.NiagaraComponent);c.set_asset(system)
prop(c,'cast_shadow',False)
c.set_collision_profile_name('NoCollision')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('MARINE_SNOW_SETUP_SUCCESS')
