import unreal,json,os
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_name()=='L_Epipelagic'
actors=EAS.get_all_level_actors()
platform=next(a for a in actors if isinstance(a,unreal.SurfaceBoardingPlatform))
eco=next(a for a in actors if isinstance(a,unreal.MarineEcosystem))
assert len(eco.get_editor_property('species_meshes'))==4
counts={}
for a in actors:
 if a.get_actor_label().startswith('Biome detail'):
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  counts[c.static_mesh.get_name()]=c.get_instance_count();assert c.get_instance_count()>0
  for i in range(min(12,c.get_instance_count())):
   t=c.get_instance_transform(i,world_space=True);assert t.rotation.rotate_vector(unreal.Vector(0,0,1)).z>.70,(a.get_actor_label(),t.rotation)
for x,y in [(25,6000),(425,6200),(-225,5850)]:
 h=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,400),unreal.Vector(x,y,100),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE)
 assert h and h.to_tuple()[0] and abs(h.to_tuple()[4].z-240)<3,(x,y,h)
assert all(a.get_editor_property('hidden') for a in actors if a.get_actor_label().startswith('Wildlife |'))
unreal.log('EXPANSION_EDITOR_PASS '+str(counts))
