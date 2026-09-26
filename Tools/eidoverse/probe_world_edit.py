import unreal, time
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b and b.endpoint=='ws://127.0.0.1:19002/ws'
started=time.monotonic()
def check(dt):
    found=False
    for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
        pos=a.get_actor_location()
        if abs(pos.x-400)<1 and abs(pos.y+400)<1 and a.get_component_by_class(unreal.StaticMeshComponent): found=True
    if found:
        s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
        assert s.entity_count==4 and s.loaded_models==2
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log('WORLD_EDIT_PASS: live place moved the realized GLB without losing models')
    elif time.monotonic()-started>8:
        unreal.unregister_slate_post_tick_callback(handle)
        raise AssertionError('live place update must reach existing mesh')
handle=unreal.register_slate_post_tick_callback(check)
