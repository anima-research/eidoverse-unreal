"""Replace prototype skins, preserve existing actor classes and interaction graphs."""
import unreal
from editor_toolset.toolsets.actor import ActorTools as A
from editor_toolset.toolsets.blueprint import BlueprintTools as B
ROOT='/Game/Underwater/Vessels'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(),'Stop PIE before installing vessel geometry.'
def prop(o,n,v):o.set_editor_property(n,v)
def components(bp):return {c.get_name().removesuffix('_GEN_VARIABLE'):c for c in A.get_components(B.get_default_object(bp))}
def vec(t):return unreal.Vector(*t)
def disable(bp,names):
 for name,c in components(bp).items():
  if name not in names:continue
  c.set_visibility(False);c.set_hidden_in_game(True)
  c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
  c.set_static_mesh(None)
def airbox(bp,name,center,extent,primary=False):
 c=components(bp).get(name)
 if not c:c=A.add_component(bp,unreal.BoxComponent.static_class(),name)
 prop(c,'relative_location',vec(center));prop(c,'box_extent',vec(extent));prop(c,'relative_rotation',unreal.Rotator())
 prop(c,'relative_scale3d',vec((1,1,1)))
 if not primary:
  prop(c,'component_tags',['WaterAirVolume'])
  c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
  prop(c,'generate_overlap_events',False)
def floor(bp,name,loc=None,scale=None):
 c=components(bp)[name]
 if loc:prop(c,'relative_location',vec(loc))
 if scale:prop(c,'relative_scale3d',vec(scale))
 c.set_material(0,unreal.load_asset(ROOT+'/Materials/M_Vessel_Graphite'))

for bpname,vessel in [('BP_Habitat','Habitat'),('BP_Cyclops','Submarine')]:
 bp=unreal.load_asset('/Game/Underwater/Blueprints/'+bpname)
 old=(['Wall_Back','Wall_L','Wall_R','Front_Bottom','Front_Top','Front_L','Front_R','Ceiling','Window','Table'] if vessel=='Habitat' else
 ['Ceiling','Wall_L','Wall_R','Wall_Rear','Bow_Bottom','Bow_Top','Bow_L','Bow_R','Bridge_Window','Helm','Tower','Keel','MeshComponent0'])
 disable(bp,old)
 for group in ('Shell','Fittings','Interior','Glass','Lettering'):
  name='Vessel_'+group
  c=components(bp).get(name)
  if not c:c=A.add_component(bp,unreal.StaticMeshComponent.static_class(),name)
  c.set_static_mesh(unreal.load_asset(ROOT+'/Meshes/SM_'+vessel+'_'+group))
  prop(c,'relative_location',unreal.Vector());prop(c,'relative_rotation',unreal.Rotator());prop(c,'relative_scale3d',vec((1,1,1)))
  prop(c,'mobility',unreal.ComponentMobility.MOVABLE)
  prop(c,'generate_overlap_events',False)
  c.set_visibility(True);c.set_hidden_in_game(False)
  if group=='Lettering':c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
  else:c.set_collision_profile_name('BlockAllDynamic')
  if group=='Glass':prop(c,'cast_shadow',False)
 if vessel=='Habitat':
  # Bury the dry-volume boundary inside the floor slab, not on its visible face.
  airbox(bp,'AirVolume',(0,0,145),(415,245,155),True)
  extras=[((0,0,330),(415,140,40)),((0,0,380),(415,70,10)),((0,0,150),(415,295,70)),
   ((-470,0,150),(55,175,90)),((470,0,150),(55,175,90)),((-555,0,150),(30,90,75)),((555,0,150),(30,90,75))]
  for i,(p,e) in enumerate(extras):airbox(bp,'AirDetail_'+str(i),p,e)
  for name in ('Floor_L','Floor_R'):floor(bp,name,scale=(3.6,4.5,.2))
  floor(bp,'Floor_F',(0,-142.5,-10),(1.2,1.65,.2));floor(bp,'Floor_B',(0,142.5,-10),(1.2,1.65,.2))
  # Put the existing lamps alongside the new warm luminaires.
  for i,x in enumerate((-250,0,250)):
   c=components(bp)['Lamp_'+str(i)];prop(c,'relative_location',vec((x,0,318)))
 else:
  airbox(bp,'AirVolume',(-870,0,-15),(840,180,165),True)
  extras=[((-970,0,0),(770,245,95)),((-980,0,190),(760,115,40)),((35,0,0),(65,130,120)),
   ((135,0,0),(35,60,70)),((-1800,0,0),(90,150,140)),((-1960,0,0),(65,45,55))]
  for i,(p,e) in enumerate(extras):airbox(bp,'AirDetail_'+str(i),p,e)
  floor(bp,'Floor_Main',scale=(15.1,3.0,.2));floor(bp,'Floor_Rear',scale=(1.3,3.6,.2))
  floor(bp,'Floor_HatchL',(-1540,-130,-180),(1.6,1,.2));floor(bp,'Floor_HatchR',(-1540,130,-180),(1.6,1,.2))
  for i,y in enumerate((-205,205)):
   c=components(bp)['Headlight_'+str(i)];prop(c,'relative_location',vec((232,y,-135)))
 B.compile_blueprint(bp)
 unreal.EditorAssetLibrary.save_loaded_asset(bp)
 unreal.log('VESSEL_INSTALL_OK '+bpname+' components='+str(len(components(bp))))

eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for a in eas.get_all_level_actors():
 cname=a.get_class().get_name()
 if cname not in ('BP_Habitat_C','BP_Cyclops_C'):continue
 bp=unreal.load_asset('/Game/Underwater/Blueprints/'+cname.removesuffix('_C'))
 defaults=components(bp)
 # Native component setters do not propagate all template edits to already placed actors.
 # Synchronize this level's existing instances explicitly; saved values match the new defaults.
 for c in a.get_components_by_class(unreal.SceneComponent):
  source=defaults.get(c.get_name())
  if not source:continue
  if c!=a.get_editor_property('root_component'):
   for key in ('relative_location','relative_rotation','relative_scale3d'):
    prop(c,key,source.get_editor_property(key))
  if isinstance(c,unreal.StaticMeshComponent):
   c.set_static_mesh(source.get_editor_property('static_mesh'))
   prop(c,'override_materials',source.get_editor_property('override_materials'))
   c.set_visibility(source.get_editor_property('visible'))
   c.set_hidden_in_game(source.get_editor_property('hidden_in_game'))
   prop(c,'cast_shadow',source.get_editor_property('cast_shadow'))
   if not source.get_editor_property('static_mesh') or c.get_name() in ('Vessel_Lettering','PoolSurface','HatchSurface'):c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
   else:c.set_collision_profile_name('BlockAllDynamic')
   if c.get_name().startswith('Vessel_'):prop(c,'generate_overlap_events',False)
  if isinstance(c,unreal.BoxComponent):
   prop(c,'box_extent',source.get_editor_property('box_extent'))
   prop(c,'component_tags',source.get_editor_property('component_tags'))
   if c.get_name().startswith('AirDetail_'):c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);prop(c,'generate_overlap_events',False)
 if cname=='BP_Habitat_C':a.set_actor_label('PELAGIC 01 | Research Habitat')
 if cname=='BP_Cyclops_C':a.set_actor_label('NEREID 07 | Expedition Submarine')
# Apply render state last using native setters. Editor transform notifications can
# reconstruct components, so reacquire every instance after the property-edit pass.
for a in eas.get_all_level_actors():
 cname=a.get_class().get_name()
 if cname not in ('BP_Habitat_C','BP_Cyclops_C'):continue
 defaults=components(unreal.load_asset('/Game/Underwater/Blueprints/'+cname.removesuffix('_C')))
 for c in a.get_components_by_class(unreal.SceneComponent):
  source=defaults.get(c.get_name())
  if not source:continue
  if c!=a.get_editor_property('root_component'):
   c.set_relative_location(source.get_editor_property('relative_location'),False,True)
   c.set_relative_rotation(source.get_editor_property('relative_rotation'),False,True)
   c.set_relative_scale3d(source.get_editor_property('relative_scale3d'))
  if isinstance(c,unreal.StaticMeshComponent):
   c.set_static_mesh(source.get_editor_property('static_mesh'))
   for i,m in enumerate(source.get_editor_property('override_materials')):c.set_material(i,m)
   c.set_visibility(source.get_editor_property('visible'))
   c.set_hidden_in_game(source.get_editor_property('hidden_in_game'))
   if not source.get_editor_property('static_mesh') or c.get_name() in ('Vessel_Lettering','PoolSurface','HatchSurface'):c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
   else:c.set_collision_profile_name('BlockAllDynamic')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('VESSEL_INSTALL_SUCCESS')
