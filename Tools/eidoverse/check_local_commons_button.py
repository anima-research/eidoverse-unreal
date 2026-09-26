"""Run after clicking Join Local Commons in a fresh PIE session; no connection setup."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert b.joined and b.world_name=='commons'
assert b.endpoint=='ws://127.0.0.1:19002/ws'
assert b.avatar_library_base=='http://127.0.0.1:19002/library/'
assert b.render_dynamic_world and b.load_remote_avatars
assert not b.identity_token and not b.share_scene_screenshots and not b.spectator
assert s and s.entity_count==80 and s.loaded_models==70 and s.failed_models==0
pc=unreal.GameplayStatics.get_player_controller(w,0)
assert not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
assert isinstance(unreal.GameplayStatics.get_player_pawn(w,0),unreal.EidoverseExplorer)
unreal.log('LOCAL_COMMONS_BUTTON_PASS: preset, 70 models, no production identity, movement released')
