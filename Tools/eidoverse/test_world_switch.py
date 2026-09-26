"""Asynchronous live scene isolation/cleanup checks, pinned to the local fixture."""
import unreal, time
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.endpoint=='ws://127.0.0.1:19002/ws'
started=time.monotonic()
stage=0
old_scene=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert old_scene.entity_count==4
assert b.switch_world('other-scene')
assert not b.share_scene_screenshots
def check(dt):
    global stage
    try:
        assert time.monotonic()-started<35,'world switch timeout'
        scene=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
        if not b.joined or not scene: return
        if stage==0 and b.world_name=='other-scene':
            assert scene.entity_count==0 and scene.loaded_models==0
            assert b.switch_world('scene-test')
            stage=1
        elif stage==1 and b.world_name=='scene-test' and scene.entity_count==4 and scene.loaded_models==2:
            b.disconnect()
            assert not unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
            assert not isinstance(unreal.GameplayStatics.get_player_pawn(w,0),unreal.EidoverseExplorer)
            for ocean in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.SpectralOcean):
                assert not ocean.get_editor_property('hidden') and ocean.is_actor_tick_enabled()
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.log('WORLD_SWITCH_PASS: empty second world, first rehydrated, consent reset, prototype restored')
    except Exception:
        unreal.unregister_slate_post_tick_callback(handle)
        raise
handle=unreal.register_slate_post_tick_callback(check)
