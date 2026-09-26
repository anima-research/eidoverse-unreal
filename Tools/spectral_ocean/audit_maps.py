import unreal,json
from pathlib import Path
eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
report={}
for path in ['/Game/Underwater/Maps/L_Epipelagic','/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype']:
    assert levels.load_level(path)
    actors=eas.get_all_level_actors()
    report[path]={'world':unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name(),'spectral':[a.get_actor_label() for a in actors if isinstance(a,unreal.SpectralOcean)],'water':[(a.get_actor_label(),a.get_editor_property('hidden')) for a in actors if a.get_actor_label()=='WaterSurface'],'skylight_intensity':[a.get_component_by_class(unreal.SkyLightComponent).get_editor_property('intensity') for a in actors if isinstance(a,unreal.SkyLight)]}
    prototype='SpectralPrototype' in path
    assert len(report[path]['spectral'])==(1 if prototype else 0),report[path]
    assert report[path]['water']==[('WaterSurface',prototype)],report[path]
    assert report[path]['skylight_intensity']==[1.0 if prototype else 0.0],report[path]
    if prototype:
        ocean=next(a for a in actors if isinstance(a,unreal.SpectralOcean))
        report[path]['performance']={
            'skip_unused_translucency_volume':ocean.get_editor_property('skip_unused_translucency_volume'),
            'shadow_page_budget':ocean.get_editor_property('shadow_page_budget')}
        assert report[path]['performance']=={'skip_unused_translucency_volume':True,'shadow_page_budget':8192},report[path]
Path('/Users/olena/unrealtest/demo_reel/spectral_map_audit.json').write_text(json.dumps(report,indent=2))
unreal.log('SPECTRAL_MAP_AUDIT_DONE')
