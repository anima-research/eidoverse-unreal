"""Query-only glass collision verification against both sides of each window."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
for cls in ('BP_Habitat_C','BP_Cyclops_C'):
 actor=next(a for a in actors if a.get_class().get_name()==cls)
 ignore=[a for a in actors if a!=actor]
 # Habitat side glazing, sub nose glazing; clear of mullions/furniture.
 paths=([((130,180,170),(130,430,170)),((130,-180,170),(130,-430,170))] if cls=='BP_Habitat_C' else
        [((0,65,80),(360,65,80))])
 transform=actor.get_actor_transform()
 for a,b in paths:
  start=unreal.MathLibrary.transform_location(transform,unreal.Vector(*a))
  end=unreal.MathLibrary.transform_location(transform,unreal.Vector(*b))
  for s,e in ((start,end),(end,start)):
   result=unreal.SystemLibrary.sphere_trace_single(w,s,e,35,unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE)
   assert result,(cls,s,e,'glass did not block')
   data=result.to_tuple()
   assert data[0] and not data[1] and data[10].get_name()=='Vessel_Glass',(cls,data)
   unreal.log('GLASS_SWEEP_PASS '+cls+' direction='+str((a,b) if s==start else (b,a)))
unreal.log('VESSEL_REFINEMENT_COLLISION_SUCCESS')
