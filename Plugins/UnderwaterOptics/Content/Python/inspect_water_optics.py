import unreal
from editor_toolset.toolsets.material import MaterialTools
worlds=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world=worlds.get_game_world() or worlds.get_editor_world()
for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
    if isinstance(a,unreal.WaterOpticsController):
        unreal.log('OPTICS_CHECK controller '+a.get_path_name()+' '+str([(p,a.get_editor_property(p)) for p in ('air_volume_count','local_light_count','registry_overflow')]))
    if isinstance(a,unreal.PostProcessVolume):
        s=a.get_editor_property('settings')
        unreal.log('OPTICS_CHECK PP '+str(s.get_editor_property('weighted_blendables'))+' '+str([(p,s.get_editor_property(p)) for p in ('auto_exposure_min_brightness','auto_exposure_max_brightness','auto_exposure_method')]))
    for c in a.get_components_by_class(unreal.BoxComponent):
        if c.get_name()=='AirVolume': unreal.log('OPTICS_CHECK AIR '+c.get_path_name()+' '+str(c.get_world_location())+' '+str(c.get_scaled_box_extent()))
    for c in a.get_components_by_class(unreal.LocalLightComponent):
        unreal.log('OPTICS_CHECK LIGHT '+c.get_path_name()+' '+str(c.get_editor_property('light_function_material'))+' '+str(c.get_editor_property('mobility')))
for name in ('M_WaterOpticsView','M_WaterOpticsLocalLight','LF_SunSpectral'):
    m=unreal.load_asset('/Game/Underwater/Materials/'+name)
    unreal.log('OPTICS_CHECK MATERIAL '+name)
    for e in MaterialTools.get_expressions(m):
        if isinstance(e,unreal.MaterialExpressionCustom):
            unreal.log('OPTICS_CHECK INPUTS '+name+' '+str(e.get_editor_property('inputs')))
unreal.log('OPTICS_CHECK_DONE')
