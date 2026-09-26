"""Install a dormant bridge only in the spectral prototype; no network at edit time."""
import unreal
root='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert levels.load_level(root)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
bridge=next((a for a in actors.get_all_level_actors() if isinstance(a,unreal.EidoverseBridge)),None)
if not bridge:bridge=actors.spawn_actor_from_class(unreal.EidoverseBridge,unreal.Vector(0,0,0))
bridge.set_actor_label('Eidoverse | Presence and chat (opt-in)')
bridge.set_editor_property('auto_connect',False)
assert levels.save_current_level()
unreal.log('EIDOVERSE_BRIDGE_INSTALLED')
