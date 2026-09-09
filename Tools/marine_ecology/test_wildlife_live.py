"""PIE verifies stock swimming components actually animate (not just defaults)."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
wildlife=[a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor) if a.get_actor_label().startswith('Wildlife |')]
before={a.get_name():a.get_actor_rotation().yaw for a in wildlife}
start=unreal.GameplayStatics.get_time_seconds(w)
def tick_anim(dt):
 if unreal.GameplayStatics.get_time_seconds(w)-start<5:return
 unreal.unregister_slate_post_tick_callback(wildlife_handle)
 try:
  for a in wildlife:
   difference=abs((a.get_actor_rotation().yaw-before[a.get_name()]+180)%360-180)
   assert difference>4,(a.get_actor_label(),difference)
   unreal.log('WILDLIFE_MOTION_PASS '+a.get_actor_label()+' yaw_delta='+str(difference))
  unreal.log('WILDLIFE_RUNTIME_SUCCESS groups='+str(len(wildlife)))
 except Exception as e:unreal.log_error('WILDLIFE_RUNTIME_FAILED '+repr(e))
wildlife_handle=unreal.register_slate_post_tick_callback(tick_anim)
