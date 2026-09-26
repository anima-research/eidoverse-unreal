"""Start PIE with the local avatar fixture; no production world is joined."""
import unreal,time,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
avatar_test_start=time.monotonic()
avatar_test_bridge=None
def avatar_test_step(dt):
 global avatar_test_bridge
 w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 if not w:return
 if avatar_test_bridge is None:
  avatar_test_bridge=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
  avatar_test_bridge.endpoint='ws://127.0.0.1:18994/ws'
  unreal.GameplayStatics.get_player_pawn(w,0).set_actor_location(unreal.Vector(7900,-2150,-700),False,True)
  unreal.GameplayStatics.get_player_controller(w,0).set_control_rotation(unreal.Rotator(pitch=0,yaw=-148,roll=0))
  avatar_test_bridge.connect()
 if avatar_test_bridge.loaded_avatar_count>=2:
  unreal.unregister_slate_post_tick_callback(avatar_test_handle)
  components=[]
  for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
   components.extend(a.get_components_by_class(unreal.EidoverseAvatarMesh))
  assert len(components)==2
  assert components[0].get_skinned_asset()==components[1].get_skinned_asset(),'duplicate mesh downloads'
  report={'joined':avatar_test_bridge.joined,'remote_count':avatar_test_bridge.remote_count,'loaded':avatar_test_bridge.loaded_avatar_count,'shared_asset':True,'bones':[c.get_num_bones() for c in components]}
  Path('/Users/olena/unrealtest/eidoverse/avatar_client_report.json').write_text(json.dumps(report,indent=2))
  unreal.log('EIDOVERSE_AVATAR_TEST_PASS '+json.dumps(report))
 elif time.monotonic()-avatar_test_start>90:
  unreal.unregister_slate_post_tick_callback(avatar_test_handle)
  unreal.log_error('EIDOVERSE_AVATAR_TEST_TIMEOUT')
def avatar_test_tick(dt):
 try:
  avatar_test_step(dt)
 except Exception:
  unreal.unregister_slate_post_tick_callback(avatar_test_handle)
  raise
avatar_test_handle=unreal.register_slate_post_tick_callback(avatar_test_tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
