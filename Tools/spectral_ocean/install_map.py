import unreal
assets=unreal.EditorAssetLibrary;root='/Game/Underwater/SpectralPrototype'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not assets.does_asset_exist(root+'/L_SpectralOcean_Prototype'):
    assert levels.load_level('/Game/Underwater/Maps/L_Epipelagic')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert unreal.EditorLoadingAndSavingUtils.save_map(world,root+'/L_SpectralOcean_Prototype')
assert levels.load_level(root+'/L_SpectralOcean_Prototype')
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith(root+'/')
for a in actors.get_all_level_actors():
    if isinstance(a,unreal.SpectralOcean):actors.destroy_actor(a)
water=next(a for a in actors.get_all_level_actors() if a.get_actor_label()=='WaterSurface')
water.set_actor_hidden_in_game(True)
for c in water.get_components_by_class(unreal.StaticMeshComponent):c.set_visibility(False)
ocean=actors.spawn_actor_from_class(unreal.SpectralOcean,unreal.Vector(0,0,0));ocean.set_actor_label('SpectralOcean_Prototype')
for n,path in [('surface_material','M_SpectralSurface'),('under_material','M_SpectralUnderside'),('optics_material','M_SpectralOptics')]:ocean.set_editor_property(n,unreal.load_asset(root+'/'+path))
ocean.set_editor_property('collection',unreal.load_asset('/Game/Underwater/Materials/MPC_Water'))
ocean.set_editor_property('wave_scale',1.5)
ocean.set_editor_property('skip_unused_translucency_volume',True)
ocean.set_editor_property('shadow_page_budget',8192)
for actor in actors.get_all_level_actors():
    sky=actor.get_component_by_class(unreal.SkyLightComponent)
    if sky:sky.set_intensity(1.0)
mesh=unreal.load_asset(root+'/SM_SpectralOceanGrid')
for component in (ocean.get_editor_property('surface_mesh'),ocean.get_editor_property('under_mesh')):
    component.set_static_mesh(mesh);component.set_collision_profile_name('NoCollision')
controller=next(a for a in actors.get_all_level_actors() if isinstance(a,unreal.WaterOpticsController))
controller.set_editor_property('spectral_ocean',ocean)
controller.set_editor_property('local_light_material',unreal.load_asset(root+'/M_SpectralLocalLight'))
controller.add_tick_prerequisite_actor(ocean)
assert levels.save_current_level()
unreal.log('SPECTRAL_PROTOTYPE_INSTALLED '+world.get_path_name())
