"""Install ocean assets after loading the rebuilt UnderwaterOptics plugin."""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
from editor_toolset.toolsets.blueprint import BlueprintTools as B
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert any(a.get_actor_label()=='WaterSurface' for a in EAS.get_all_level_actors()),'Open L_Epipelagic before installing'
MEL=unreal.MaterialEditingLibrary;AT=unreal.AssetToolsHelpers.get_asset_tools()
ROOT='/Game/Underwater/Materials'
def prop(o,n,v):o.set_editor_property(n,v)
def struct(cls,**kw):
 o=cls()
 for k,v in kw.items():prop(o,k,v)
 return o
def expr(mat,cls,**kw):
 o=MEL.create_material_expression(mat,cls)
 for k,v in kw.items():prop(o,k,v)
 return o
collection=unreal.load_asset(ROOT+'/MPC_Water')
for field,cls,items in [('scalar_parameters',unreal.CollectionScalarParameter,{'OceanTime':0.,'OceanEnabled':1.}),('vector_parameters',unreal.CollectionVectorParameter,{'OceanSubCenter':unreal.LinearColor(0,0,0,0),'OceanSubX':unreal.LinearColor(1,0,0,0),'OceanSubY':unreal.LinearColor(0,1,0,0)})]:
 values=list(collection.get_editor_property(field));names={str(v.get_editor_property('parameter_name')) for v in values}
 for name,default in items.items():
  if name not in names:values.append(struct(cls,parameter_name=name,default_value=default))
 prop(collection,field,values)
unreal.EditorAssetLibrary.save_loaded_asset(collection)

# Thread the shared wave clock into all existing water integrations, preserving
# every other material parameter and connection. Old regression material stays flat.
for path in [ROOT+'/M_WaterOpticsView',ROOT+'/M_WaterOpticsLocalLight',ROOT+'/LF_SunSpectral','/Game/Underwater/Tests/M_WaterOpticsRegression']:
 mat=unreal.load_asset(path)
 if not mat:continue
 for custom in M.get_expressions(mat):
  if not isinstance(custom,unreal.MaterialExpressionCustom):continue
  code=custom.get_editor_property('code')
  if 'FWaterOptics w;' not in code:continue
  code=code.replace('water[17]','water[40]')
  if 'w.OceanTime=' not in code:
   code=code.replace('FWaterOptics w;','FWaterOptics w; w.OceanTime=OceanTime; w.OceanEnabled=OceanEnabled;')
   inputs=list(custom.get_editor_property('inputs'))
   names={str(x.get_editor_property('input_name')) for x in inputs}
   for name in ('OceanTime','OceanEnabled'):
    if name not in names:inputs.append(struct(unreal.CustomInput,input_name=name))
   prop(custom,'inputs',inputs);prop(custom,'code',code)
   for name in ('OceanTime','OceanEnabled'):
    node=expr(mat,unreal.MaterialExpressionConstant,r=0) if '/Tests/' in path else expr(mat,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name=name)
    assert MEL.connect_material_expressions(node,'',custom,name)
  else:prop(custom,'code',code)
 MEL.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)

path=ROOT+'/M_OceanSurface';mat=unreal.load_asset(path)
if not mat:mat=AT.create_asset('M_OceanSurface',ROOT,unreal.Material,unreal.MaterialFactoryNew())
else:
 for e in M.get_expressions(mat):MEL.delete_material_expression(mat,e)
prop(mat,'blend_mode',unreal.BlendMode.BLEND_MASKED);prop(mat,'two_sided',True);prop(mat,'tangent_space_normal',False)
inputs={'WorldPos':expr(mat,unreal.MaterialExpressionWorldPosition),'CamPos':expr(mat,unreal.MaterialExpressionCameraPositionWS)}
for name,param in [('Time','OceanTime'),('Enabled','OceanEnabled'),('Surface','OpticsSurfaceZ'),('SubC','OceanSubCenter'),('SubX','OceanSubX'),('SubY','OceanSubY')]:
 inputs[name]=expr(mat,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name=param)
init='FWaterOptics air;\n'
for i in range(16):
 for suffix in ('C','E','X','Y'):
  name=f'Air{i}{suffix}';inputs[name]=expr(mat,unreal.MaterialExpressionCollectionParameter,collection=collection,parameter_name='Optics'+name)
  init+=f'air.{suffix}[{i}]={name}'+(';' if suffix=='C' else '.xyz;')+'\n'
code=init+'''
FOceanWaves waves;
float3 swell=waves.Sample(WorldPos.xy,Time)*Enabled;
Displacement=float3(0,0,swell.x);
float2 gradient=swell.yz;
// Incommensurate wavelengths/directions replace the repeated normal-map tile.
float fade=1.0-smoothstep(12000.0,80000.0,length(CamPos-WorldPos));
[unroll] for(int i=0;i<7;i++) {
    float angle=0.73+float(i)*2.399963;
    float2 d=float2(cos(angle),sin(angle));
    float wavelength=47.0+float(i)*23.713;
    float k=6.283185307/wavelength;
    float phase=dot(WorldPos.xy,d)*k-Time*sqrt(980.0*k)+float(i)*1.618;
    gradient+=d*cos(phase)*0.022*fade;
}
float3 normal=normalize(float3(-gradient,1));
float3 atSurface=float3(WorldPos.xy,Surface+swell.x);
Mask=1.0;
[loop] for(int i=0;i<16;i++) {
    float2 hit;
    if(air.BoxInterval(atSurface,float3(0,0,1),i,hit) && hit.x<0 && hit.y>0) Mask=0;
}
// Exclude the submarine's pressure-hull cross-section at the waterline as well
// as its room boxes, so ocean geometry never slices through the occupied cabin.
if(SubC.a>0.5) {
    float3 z=normalize(cross(SubX,SubY));
    float3 r=atSurface-SubC.xyz;
    float3 q=float3(dot(r,SubX),dot(r,SubY),dot(r,z));
    float f=1;
    if(q.x < -1750.0) f=sqrt(saturate(1.0-pow((q.x+1750.0)/330.0,2)));
    if(q.x > -80.0) f=sqrt(saturate(1.0-pow((q.x+80.0)/330.0,2)))*0.98;
    if(q.x>-2070 && q.x<245 && dot(q.yz/float2(278,263),q.yz/float2(278,263))<f*f) Mask=0;
}
float foam=0.0; // Calm/moderate swells: no painted-on circular foam patches.
Base=lerp(float3(0.008,0.042,0.065),float3(0.45,0.55,0.56),foam);
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
custom=expr(mat,unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,include_file_paths=['/Plugin/UnderwaterOptics/WaterOptics.ush'])
prop(custom,'inputs',[struct(unreal.CustomInput,input_name=n) for n in inputs])
prop(custom,'additional_outputs',[struct(unreal.CustomOutput,output_name=n,output_type=t) for n,t in [('Displacement',unreal.CustomMaterialOutputType.CMOT_FLOAT3),('Mask',unreal.CustomMaterialOutputType.CMOT_FLOAT1),('Base',unreal.CustomMaterialOutputType.CMOT_FLOAT3),('Emissive',unreal.CustomMaterialOutputType.CMOT_FLOAT3)]])
for n,e in inputs.items():assert MEL.connect_material_expressions(e,'',custom,n)
for output,p in [('',unreal.MaterialProperty.MP_NORMAL),('Displacement',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET),('Mask',unreal.MaterialProperty.MP_OPACITY_MASK),('Base',unreal.MaterialProperty.MP_BASE_COLOR),('Emissive',unreal.MaterialProperty.MP_EMISSIVE_COLOR)]:assert MEL.connect_material_property(custom,output,p)
for p,v in [(unreal.MaterialProperty.MP_ROUGHNESS,.19),(unreal.MaterialProperty.MP_SPECULAR,.65)]:MEL.connect_material_property(expr(mat,unreal.MaterialExpressionConstant,r=v),'',p)
MEL.layout_material_expressions(mat);MEL.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)

task=unreal.AssetImportTask();prop(task,'filename','/Users/olena/unrealtest/vessel_redesign/exports/SM_OceanSurface.fbx');prop(task,'destination_path','/Game/Underwater/Vessels/Meshes');prop(task,'destination_name','SM_OceanSurface');prop(task,'automated',True);prop(task,'replace_existing',True);prop(task,'save',True)
options=unreal.FbxImportUI()
for n,v in [('import_mesh',True),('import_materials',False),('import_textures',False),('import_as_skeletal',False),('automated_import_should_detect_type',False),('mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)]:prop(options,n,v)
sm=options.get_editor_property('static_mesh_import_data')
for n,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('convert_scene',True),('convert_scene_unit',True)]:prop(sm,n,v)
prop(task,'options',options);prop(task,'factory',unreal.FbxFactory());AT.import_asset_tasks([task])
mesh=unreal.load_asset('/Game/Underwater/Vessels/Meshes/SM_OceanSurface')
mesh.set_material(0,mat);prop(mesh,'positive_bounds_extension',unreal.Vector(0,0,100));prop(mesh,'negative_bounds_extension',unreal.Vector(0,0,100));unreal.EditorAssetLibrary.save_loaded_asset(mesh)
water=next(a for a in EAS.get_all_level_actors() if a.get_actor_label()=='WaterSurface')
c=water.get_component_by_class(unreal.StaticMeshComponent)
c.set_static_mesh(mesh);c.set_relative_scale3d(unreal.Vector(1,1,1));c.set_material(0,mat);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
prop(c,'render_custom_depth',True);prop(c,'custom_depth_stencil_value',1);prop(c,'cast_shadow',False)
for bpname,tag in [('BP_DiverPawn','OceanDiver'),('BP_Cyclops','OceanSubmarine')]:
 bp=unreal.load_asset('/Game/Underwater/Blueprints/'+bpname);d=B.get_default_object(bp)
 tags=list(d.get_editor_property('tags'))
 if tag not in tags:tags.append(tag)
 prop(d,'tags',tags);B.compile_blueprint(bp);unreal.EditorAssetLibrary.save_loaded_asset(bp)
 for a in EAS.get_all_level_actors():
  if a.get_class().get_name()==bpname+'_C':
   tags=list(a.tags)
   if tag not in tags:tags.append(tag)
   prop(a,'tags',tags)
controller=next(a for a in EAS.get_all_level_actors() if isinstance(a,unreal.WaterOpticsController))
prop(controller,'enable_ocean_surface',True)
# Apply a persistent collision profile last: editor property notifications above
# can reconstruct the component and restore its previous BlockAll profile.
water.get_component_by_class(unreal.StaticMeshComponent).set_collision_profile_name('NoCollision')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('OCEAN_SETUP_SUCCESS')
