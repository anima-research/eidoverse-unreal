"""Run against tools/render-scene-wire-test.ts --native; no production joins."""
import unreal
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    level.editor_request_begin_play()
def setup(delta):
    world=editor.get_game_world()
    if not world: return
    bridges=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.EidoverseBridge)
    if not bridges: return
    b=bridges[0]
    b.disconnect()
    b.set_editor_property('endpoint','ws://127.0.0.1:19002/ws')
    b.set_editor_property('world_name','scene-test')
    b.set_editor_property('participant_name','unreal-scene-test')
    b.set_editor_property('join_token','scene-test-door')
    b.set_editor_property('identity_token','')
    b.set_editor_property('render_dynamic_world',True)
    b.set_editor_property('avatar_library_base','http://127.0.0.1:19002/library/')
    b.set_editor_property('load_remote_avatars',globals().get('LOAD_TEST_AVATARS',False))
    b.connect()
    unreal.log('EIDOVERSE_DYNAMIC_TEST_STARTED')
    unreal.unregister_slate_post_tick_callback(handle)
handle=unreal.register_slate_post_tick_callback(setup)
