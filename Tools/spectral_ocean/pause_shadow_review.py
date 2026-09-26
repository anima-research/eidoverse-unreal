"""Return the prototype to its pre-experiment mesh bindings; keep candidate copies."""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Underwater/SpectralPrototype/')
count=0
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 for component in actor.get_components_by_class(unreal.StaticMeshComponent):
  mesh=component.get_editor_property('static_mesh')
  if mesh and '/SpectralPrototype/ShadowMeshes/' in mesh.get_path_name():
   original=unreal.load_asset('/Game/Underwater/Ecology/Meshes/'+mesh.get_name())
   assert original
   component.set_static_mesh(original);count+=1
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
settings.set_editor_property('bThrottleCPUWhenNotForeground',True)
for command in ['r.ProfileGPU.ShowUI 1','r.ProfileGPU.ShowEventHistogram 0']:
 unreal.SystemLibrary.execute_console_command(world,command)
unreal.log('SHADOW_REVIEW_PAUSED_RESTORED '+str(count))
