"""Startup entry point for the local-only night preview; no persistent world edits."""
import unreal,time
def start():
    editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    state={'joining':False,'started':time.monotonic()}
    def tick(dt):
        if time.monotonic()-state['started']>90:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal.log_error('Night preview timed out waiting for local Commons')
            return
        world=editor.get_game_world()
        if not world:return
        bridge=unreal.GameplayStatics.get_actor_of_class(world,unreal.EidoverseBridge)
        if not bridge:return
        if not state['joining']:
            state['joining']=True;bridge.join_local_commons()
            return
        if not bridge.joined:return
        assert bridge.endpoint=='ws://127.0.0.1:19002/ws' and bridge.world_name=='commons'
        scene=unreal.GameplayStatics.get_actor_of_class(world,unreal.EidoverseScene)
        if not scene:return
        scene.set_preview_hour(22)
        unreal.unregister_slate_post_tick_callback(state['handle'])
        assert scene.weather_hour==22
        unreal.log('COMMONS_NIGHT_READY: local rendering held at 22:00; server clock unchanged')
    state['handle']=unreal.register_slate_post_tick_callback(tick)
start()
