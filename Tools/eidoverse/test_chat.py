"""Start isolated chat UI QA; requires chat_fixture_server.ts, never joins production."""
import unreal
import time
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
started=time.monotonic()
bridge=None
def tick(dt):
 global bridge
 try:
  world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
  if not world:return
  if bridge is None:
   bridge=unreal.GameplayStatics.get_actor_of_class(world,unreal.EidoverseBridge)
   bridge.endpoint='ws://127.0.0.1:18995/ws'
   bridge.world_name='water-local-test'
   bridge.connect()
  if bridge.joined and len(bridge.chat_history)>=2:
   unreal.unregister_slate_post_tick_callback(handle)
   assert len(bridge.chat_history)==2,'duplicate public message'
   assert not bridge.chat_history[0].private
   assert bridge.chat_history[1].private
   assert not bridge.say('  '),'empty message accepted'
   bridge.open_chat()
   unreal.log('EIDOVERSE_CHAT_READY: public/private receipt, deduplication and blank rejection passed; UI open for send/focus QA')
  elif time.monotonic()-started>30:
   raise RuntimeError('chat fixture timeout')
 except Exception:
  unreal.unregister_slate_post_tick_callback(handle)
  raise
handle=unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
