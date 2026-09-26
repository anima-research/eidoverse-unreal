"""Real native-client round trip against the opt-in localhost sequencer/probe."""
import unreal,json,time
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
test_phase=0;test_start=time.monotonic();test_bridge=None;test_chat=[]
def hear(sender,text):test_chat.append({'sender':sender,'text':text})
def tick(dt):
 global test_phase,test_bridge,test_start
 try:
  w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
  if not w:return
  if test_phase==0:
   test_bridge=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge);assert test_bridge
   test_bridge.on_chat.add_callable(hear)
   pawn=unreal.GameplayStatics.get_player_pawn(w,0)
   pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True)
   unreal.GameplayStatics.get_player_controller(w,0).set_control_rotation(unreal.Rotator(pitch=0,yaw=-148,roll=0))
   test_bridge.connect();test_phase=1
  elif test_phase==1 and test_bridge.joined and test_bridge.remote_count>0:
   assert test_bridge.say('Native Unreal underwater integration test')
   test_phase=2
  elif test_phase==2 and any(c['sender']=='eidoverse-test-peer' for c in test_chat) and test_bridge.sent_pose_count>20:
   report={'joined':test_bridge.joined,'you':test_bridge.assigned_identity,'remotes':test_bridge.remote_count,'poses_sent':test_bridge.sent_pose_count,'chat_received':test_chat}
   Path('/Users/olena/unrealtest/eidoverse/native_client_report.json').write_text(json.dumps(report,indent=2))
   unreal.unregister_slate_post_tick_callback(test_handle)
   unreal.log('EIDOVERSE_NATIVE_TEST_PASS '+json.dumps(report))
  if time.monotonic()-test_start>30:raise RuntimeError('native bridge test timeout')
 except Exception as error:
  unreal.unregister_slate_post_tick_callback(test_handle);unreal.log_error(str(error));raise
test_handle=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
