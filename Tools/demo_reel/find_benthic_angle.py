import unreal
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
crabs=next(a for a in actors if a.get_actor_label()=='Biome detail | Crab').get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
plants=[]
for a in actors:
 if any(n in a.get_actor_label() for n in ('GiantKelp','UnderstoryKelp','RedAlgae')):
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  if c:
   plants.extend(c.get_instance_transform(i,world_space=True).translation for i in range(c.get_instance_count()))
rank=[]
for i in range(crabs.get_instance_count()):
 p=crabs.get_instance_transform(i,world_space=True).translation
 d=min((p-q).length() for q in plants)
 rank.append((d,i,str(p)))
for d,i,p in sorted(rank,reverse=True)[:8]:unreal.log('REEL_CRAB_CHOICE '+str((i,round(d),p)))
