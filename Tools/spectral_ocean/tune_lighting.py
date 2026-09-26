import unreal
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Underwater/SpectralPrototype/')
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    sky=a.get_component_by_class(unreal.SkyLightComponent)
    if sky:
        sky.set_intensity(1.0)
        sky.set_editor_property('real_time_capture',True)
        sky.recapture_sky()
    if isinstance(a,unreal.SpectralOcean):a.set_editor_property('wave_scale',1.5)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('SPECTRAL_SKY_RESTORED')
