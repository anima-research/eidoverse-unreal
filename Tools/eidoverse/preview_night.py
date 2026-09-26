"""Hold only the local Commons renderer at 22:00; no server/history writes.
To restore the server clock: scene.set_preview_hour(-1), or stop/rejoin Play.
"""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b and b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert s
s.set_preview_hour(22)
assert s.weather_hour==22 and s.preview_hour==22
lights=[c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
        if not a.get_editor_property('hidden')
        for c in a.get_components_by_class(unreal.DirectionalLightComponent)]
assert lights and all(c.get_editor_property('intensity')==0 for c in lights)
unreal.log('COMMONS_NIGHT_READY: local rendering held at 22:00; server clock unchanged')
