"""Live scratch-only QA: weather, flora, use, native lease simulation, reach."""
import unreal, time
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseBridge)[0]
assert b.get_editor_property('endpoint')=='ws://127.0.0.1:19002/ws'
s=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseScene)[0]
assert s.get_editor_property('flora_count')>100
assert s.get_editor_property('weather_state')=='rain'
assert abs(s.get_editor_property('weather_hour')-14)<.01
assert s.get_editor_property('loaded_models')==2
b.set_editor_property('load_remote_avatars',True)
p=unreal.GameplayStatics.get_player_pawn(w,0)
p.set_actor_location(unreal.Vector(220,-200,100),False,False)
assert b.use_entity('moving-crate','push')
started=time.monotonic()
def kick(delta):
    if time.monotonic()-started<1:return
    assert b.punt_entity('crate',3)
    assert b.reach_to('reach-test','shoulder_l',False)
    unreal.log('EIDOVERSE_INTERACTION_REQUESTS_PASS')
    unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(kick)
