"""Read-only placement/collision audit for the installed ecology."""
import unreal,sys,os,math,json
sys.path.insert(0,os.path.dirname(__file__))
from ecology_layout import height
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);actors=EAS.get_all_level_actors()
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
terrain=next(a for a in actors if a.get_actor_label().startswith('Temperate Shelf'))
ignore=[a for a in actors if a!=terrain]
for x,y in [(7560,963),(8440,963),(7560,1637),(8440,1637),(6000,-2600),(0,6000),(8000,24000),(4200,-700)]:
 h=height(x,y)
 result=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,500),unreal.Vector(x,y,-25000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE)
 assert result and result.to_tuple()[0],('No terrain collision',x,y)
 actual=result.to_tuple()[4].z
 assert abs(h-actual)<15,(x,y,h,actual)
 unreal.log('ECOLOGY_TERRAIN_PASS '+str((x,y,actual)))
for a in actors:
 if 'MarineEcology' not in [str(t) for t in a.tags]:continue
 c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
 if c:
  assert c.get_instance_count()>0,a.get_actor_label()
  if 'Kelp' in a.get_actor_label() or 'Algae' in a.get_actor_label() or 'Eelgrass' in a.get_actor_label():
   assert c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION,a.get_actor_label()
 if a.get_actor_label().startswith('Wildlife |'):
  m=a.get_component_by_class(unreal.RotatingMovementComponent);assert m
  assert abs(m.get_editor_property('rotation_rate').yaw)>0
diver=next(a for a in actors if a.get_class().get_name()=='BP_DiverPawn_C');p=diver.get_actor_location()
assert p.z>height(p.x,p.y)+100,p
data=json.load(open(os.path.join(os.path.dirname(__file__),'installed_layout.json')))
assert data['seagrass']>0
assert all(z+h<-250 for x,y,z,h in data['kelp'])
assert all(z>-3200 for x,y,z,h in data['kelp'])
unreal.log('ECOLOGY_VERIFY_SUCCESS '+str(data['instances']))
