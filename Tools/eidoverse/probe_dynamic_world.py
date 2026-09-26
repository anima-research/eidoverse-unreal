import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b and b.endpoint=='ws://127.0.0.1:19002/ws'
s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
p=unreal.GameplayStatics.get_player_pawn(w,0)
assert s and s.entity_count==4 and s.loaded_models==2 and s.failed_models==0
assert isinstance(p,unreal.EidoverseExplorer)
assert 80<p.get_actor_location().z<110,'grounded explorer'
for ocean in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.SpectralOcean):
    assert ocean.get_editor_property('hidden') and not ocean.is_actor_tick_enabled()
for optics in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.WaterOpticsController):
    assert not optics.is_actor_tick_enabled()
b.open_chat()
b.set_share_scene_screenshots(True)
unreal.log('DYNAMIC_WORLD_PROBE_PASS: four entities, two lit models, grounded pawn, aquatic actors suspended, sharing opted in')
unreal.log(unreal.SystemLibrary.line_trace_single.__doc__)
