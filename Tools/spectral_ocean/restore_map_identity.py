"""One-time repair: save_map copies a world but does not switch the open map."""
import unreal
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level('/Game/Underwater/Maps/L_Epipelagic')
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in eas.get_all_level_actors():
    if isinstance(actor,unreal.WaterOpticsController):
        actor.set_editor_property('spectral_ocean',None)
        actor.set_editor_property('local_light_material',unreal.load_asset('/Game/Underwater/Materials/M_WaterOpticsLocalLight'))
    if isinstance(actor,unreal.SpectralOcean) and actor.get_actor_label()=='SpectralOcean_Prototype':
        eas.destroy_actor(actor)
    if actor.get_actor_label()=='WaterSurface':
        actor.set_actor_hidden_in_game(False)
        for component in actor.get_components_by_class(unreal.StaticMeshComponent):component.set_visibility(True)
assert levels.save_current_level()
assert not any(isinstance(a,unreal.SpectralOcean) for a in eas.get_all_level_actors())
assert all(not a.get_editor_property('hidden') for a in eas.get_all_level_actors() if a.get_actor_label()=='WaterSurface')
unreal.log('ORIGINAL_MAP_RESTORED')
