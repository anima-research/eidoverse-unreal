"""Preserve soft particles in motion; extinguish distant glass through real water."""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
MEL=unreal.MaterialEditingLibrary
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def prop(o,n,v):o.set_editor_property(n,v)
def expr(mat,cls,**kw):
 o=MEL.create_material_expression(mat,cls)
 for k,v in kw.items():prop(o,k,v)
 return o
def struct(cls,**kw):
 o=cls()
 for k,v in kw.items():prop(o,k,v)
 return o
snow=unreal.load_asset('/Game/Underwater/FX/M_MarineSnow')
for k,v in [('enable_responsive_aa',True),('output_translucent_velocity',False),('opacity_mask_clip_value',.3333),('translucency_pass',unreal.MaterialTranslucencyPass.MTP_AFTER_MOTION_BLUR)]:prop(snow,k,v)
MEL.recompile_material(snow);unreal.EditorAssetLibrary.save_loaded_asset(snow)

glass=unreal.load_asset('/Game/Underwater/Materials/M_Glass')
existing=next((e for e in M.get_expressions(glass) if isinstance(e,unreal.MaterialExpressionCustom) and e.get_editor_property('description')=='Water-path glass visibility'),None)
if not existing:
 opacity=M.get_property_input(glass,unreal.MaterialProperty.MP_OPACITY)
 refraction=M.get_property_input(glass,unreal.MaterialProperty.MP_REFRACTION)
 collection=unreal.load_asset('/Game/Underwater/Materials/MPC_Water')
 inputs={'WorldPos':expr(glass,unreal.MaterialExpressionWorldPosition),'CamPos':expr(glass,unreal.MaterialExpressionCameraPositionWS)}
 for name,param in [('WP','WaterParams'),('TP','TurbidityProfile'),('Surface','OpticsSurfaceZ'),('OceanTime','OceanTime'),('OceanEnabled','OceanEnabled')]:
  inputs[name]=expr(glass,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name=param)
 inputs['LUT']=expr(glass,unreal.MaterialExpressionTextureObject,texture=unreal.load_asset('/Game/Underwater/Textures/T_WaterSpectralLUT'))
 view=unreal.load_asset('/Game/Underwater/Materials/M_WaterOpticsView')
 scatter=next(e.get_editor_property('default_value') for e in M.get_expressions(view) if isinstance(e,unreal.MaterialExpressionVectorParameter) and str(e.get_editor_property('parameter_name'))=='Scatter_PerMetre')
 inputs['Bsc']=expr(glass,unreal.MaterialExpressionVectorParameter,parameter_name='Scatter_PerMetre',default_value=scatter)
 code='FWaterOptics w; w.WP=WP; w.TP=TP; w.Surface=Surface; w.Bsc=Bsc; w.OceanTime=OceanTime; w.OceanEnabled=OceanEnabled;\n'
 for i in range(16):
  for suffix in ('C','E','X','Y'):
   name=f'Air{i}{suffix}';inputs[name]=expr(glass,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name='Optics'+name)
   code+=f'w.{suffix}[{i}]={name}'+(';' if suffix=='C' else '.xyz;')+'\n'
 # Separate translucency is composited over the already-fogged background.
 # Fade its contrast by the *camera-to-pane* water transmission, not by opaque
 # scene depth (which would incorrectly use the far seabed behind a near pane).
 # Scalar luminance is a contrast approximation for default-lit glass; avoids
 # double-scattering the background while preserving its existing reflections.
 code+='return saturate(dot(w.SourceTransmission(LUT,LUTSampler,CamPos,WorldPos),float3(0.2126,0.7152,0.0722)));'
 visibility=expr(glass,unreal.MaterialExpressionCustom,description='Water-path glass visibility',code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,include_file_paths=['/Plugin/UnderwaterOptics/WaterOptics.ush'])
 prop(visibility,'inputs',[struct(unreal.CustomInput,input_name=n) for n in inputs])
 for name,node in inputs.items():assert MEL.connect_material_expressions(node,'',visibility,name)
 mul=expr(glass,unreal.MaterialExpressionMultiply)
 assert MEL.connect_material_expressions(opacity.expression,opacity.output_name,mul,'A')
 assert MEL.connect_material_expressions(visibility,'',mul,'B')
 assert MEL.connect_material_property(mul,'',unreal.MaterialProperty.MP_OPACITY)
 if refraction.expression:
  neutral=expr(glass,unreal.MaterialExpressionConstant,r=1.)
  lerp=expr(glass,unreal.MaterialExpressionLinearInterpolate)
  MEL.connect_material_expressions(neutral,'',lerp,'A')
  MEL.connect_material_expressions(refraction.expression,refraction.output_name,lerp,'B')
  MEL.connect_material_expressions(visibility,'',lerp,'Alpha')
  MEL.connect_material_property(lerp,'',unreal.MaterialProperty.MP_REFRACTION)
MEL.layout_material_expressions(glass);MEL.recompile_material(glass)
unreal.EditorAssetLibrary.save_loaded_asset(glass)
unreal.EditorAssetLibrary.save_asset('/Game/Underwater/FX/NS_MarineSnow')
unreal.log('TRANSLUCENCY_REFINEMENT_SUCCESS')
