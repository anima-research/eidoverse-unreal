import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
platform=unreal.GameplayStatics.get_actor_of_class(w,unreal.SurfaceBoardingPlatform)
diver=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.actor_has_tag('OceanDiver'))
assert platform.boarding_state==2
assert platform.try_use_ladder(diver)
start=unreal.GameplayStatics.get_time_seconds(w)
def done(dt):
 if unreal.GameplayStatics.get_time_seconds(w)-start<3.8:return
 unreal.unregister_slate_post_tick_callback(down_handle)
 assert platform.boarding_state==0,platform.boarding_state
 assert not diver.actor_has_tag('SurfaceBoarded')
 assert diver.get_actor_location().z<0,diver.get_actor_location()
 unreal.log('PLATFORM_DESCENT_PASS '+str(diver.get_actor_location()))
down_handle=unreal.register_slate_post_tick_callback(done)
