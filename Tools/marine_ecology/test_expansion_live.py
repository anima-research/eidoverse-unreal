"""Run in PIE: independent fish movement, collision, ladder boarding."""
import unreal,math
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
eco=unreal.GameplayStatics.get_actor_of_class(w,unreal.MarineEcosystem);assert eco
platform=unreal.GameplayStatics.get_actor_of_class(w,unreal.SurfaceBoardingPlatform);assert platform
pc=unreal.GameplayStatics.get_player_controller(w,0)
diver=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.actor_has_tag('OceanDiver'))
assert eco.fish_count==478,eco.fish_count
groups=eco.get_components_by_class(unreal.InstancedStaticMeshComponent)
assert len(groups)==4
before=[c.get_instance_transform(0,world_space=True).translation for c in groups]
pair_before=(groups[0].get_instance_transform(0,world_space=True).translation-groups[0].get_instance_transform(1,world_space=True).translation).length()
diver.set_actor_location(unreal.Vector(0,5395,-100),False,True)
pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0))
assert platform.try_use_ladder(diver),'Ladder rejected approach'
start=unreal.GameplayStatics.get_time_seconds(w)
def check(dt):
 global handle
 if unreal.GameplayStatics.get_time_seconds(w)-start<4.5:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:
  assert platform.boarding_state==2,platform.boarding_state
  p=diver.get_actor_location();assert abs(p.z-330)<3,p
  assert diver.actor_has_tag('SurfaceBoarded')
  moved=[(c.get_instance_transform(0,world_space=True).translation-p0).length() for c,p0 in zip(groups,before)]
  assert min(moved)>20,moved
  pair_after=(groups[0].get_instance_transform(0,world_space=True).translation-groups[0].get_instance_transform(1,world_space=True).translation).length()
  assert abs(pair_after-pair_before)>2,(pair_before,pair_after)
  unreal.log('EXPANSION_LIVE_PASS fish='+str(eco.fish_count)+' movement='+str(moved)+' neighbor_distance_change='+str(pair_after-pair_before)+' deck='+str(p))
 except Exception as e:unreal.log_error('EXPANSION_LIVE_FAIL '+str(e));raise
handle=unreal.register_slate_post_tick_callback(check)
unreal.log('EXPANSION_LIVE_TEST_STARTED')
