"""PIE-only continuous camera motion for visual particle persistence checks."""
import unreal,math
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
pc=unreal.GameplayStatics.get_player_controller(w,0)
diver=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.get_class().get_name()=='BP_DiverPawn_C')
pc.possess(diver);diver.get_movement_component().stop_movement_immediately()
diver.set_actor_location(unreal.Vector(4500,-1000,-2500),False,True)
torch=next(c for c in diver.get_components_by_class(unreal.SpotLightComponent) if c.get_name()=='Torch');torch.set_visibility(False)
start=unreal.GameplayStatics.get_time_seconds(w)
def tick_motion(dt):
 try:
  elapsed=unreal.GameplayStatics.get_time_seconds(w)-start
  pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=35+elapsed*30,roll=0))
  diver.add_movement_input(unreal.Vector(1,0,0),.25,True)
  if elapsed>=60:
   unreal.unregister_slate_post_tick_callback(motion_handle)
   diver.get_movement_component().stop_movement_immediately();torch.set_visibility(True)
   unreal.log('PARTICLE_MOTION_TEST_FINISHED 60s continuous yaw and translation')
 except Exception as e:
  unreal.unregister_slate_post_tick_callback(motion_handle)
  unreal.log_error('PARTICLE_MOTION_TEST_FAILED '+repr(e))
motion_handle=unreal.register_slate_post_tick_callback(tick_motion)
