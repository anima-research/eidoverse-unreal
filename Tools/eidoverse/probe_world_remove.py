import unreal, time
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b and b.endpoint=='ws://127.0.0.1:19002/ws'
started=time.monotonic()
def check(dt):
    s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
    if s.entity_count==3 and s.loaded_models==1:
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log('WORLD_REMOVE_PASS: live remove retired one model, shared remaining model retained')
    elif time.monotonic()-started>8:
        unreal.unregister_slate_post_tick_callback(handle)
        raise AssertionError('live removal must retire the model')
handle=unreal.register_slate_post_tick_callback(check)
