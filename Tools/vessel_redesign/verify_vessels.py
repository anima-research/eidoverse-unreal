"""Editor checks. Temporary spawn probes are destroyed in finally; no persistent actors added."""
import unreal
from editor_toolset.toolsets.actor import ActorTools as A
from editor_toolset.toolsets.blueprint import BlueprintTools as B
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
for bpname in ('BP_Habitat','BP_Cyclops'):
 bp=unreal.load_asset('/Game/Underwater/Blueprints/'+bpname)
 for c in A.get_components(B.get_default_object(bp)):
  if isinstance(c,unreal.BoxComponent):c.set_editor_property('visible',False)
 B.compile_blueprint(bp);unreal.EditorAssetLibrary.save_loaded_asset(bp)
 probe=None
 try:
  probe=eas.spawn_actor_from_class(unreal.load_class(None,f'/Game/Underwater/Blueprints/{bpname}.{bpname}_C'),unreal.Vector(0,0,20000),transient=True)
  meshes={c.get_name():c for c in probe.get_components_by_class(unreal.StaticMeshComponent)}
  for group in ('Shell','Glass','Fittings','Interior','Lettering'):
   assert meshes['Vessel_'+group].get_editor_property('static_mesh'),(bpname,group,'new spawn missing mesh')
  for c in probe.get_components_by_class(unreal.BoxComponent):c.set_visibility(False)
  unreal.log('VESSEL_VERIFY_PASS new Blueprint spawn '+bpname)
 finally:
  if probe:eas.destroy_actor(probe)

# Keep the old calibration samples recoverable, but no longer floating in the furnished room.
for a in eas.get_all_level_actors():
 if a.get_actor_label().startswith('HabSphere_'):
  a.set_actor_hidden_in_game(True)
  for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
  a.set_folder_path('Legacy/Hidden habitat calibration')
 if a.get_class().get_name() in ('BP_Habitat_C','BP_Cyclops_C'):
  for c in a.get_components_by_class(unreal.BoxComponent):c.set_visibility(False)

def trace(start,end):
 return unreal.SystemLibrary.line_trace_single(world,unreal.Vector(*start),unreal.Vector(*end),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE)
assert trace((8000,1300,-3300),(8000,1300,-2850)) is None,'Habitat moonpool obstructed'
unreal.log('VESSEL_VERIFY_PASS habitat moonpool open')
assert trace((6000,-4140,-2300),(6000,-4140,-1880)) is None,'Submarine entry obstructed'
unreal.log('VESSEL_VERIFY_PASS submarine entry open')
assert trace((8380,2050,-2850),(8380,1300,-2850)) is not None,'Habitat pressure shell has no collision'
unreal.log('VESSEL_VERIFY_PASS habitat shell blocks trace')
assert trace((6700,-3400,-1800),(6000,-3400,-1800)) is not None,'Submarine pressure shell has no collision'
unreal.log('VESSEL_VERIFY_PASS submarine shell blocks trace')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('VESSEL_VERIFY_SUCCESS')
