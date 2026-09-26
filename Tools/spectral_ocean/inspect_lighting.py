import unreal,json
from pathlib import Path
data=[]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    c=a.get_component_by_class(unreal.SkyLightComponent)
    if c:
        d={'actor':a.get_actor_label(),'class':a.get_class().get_name()}
        for n in ('intensity','source_type','real_time_capture','affects_world','visible','lower_hemisphere_is_black','cubemap'):
            try:d[n]=str(c.get_editor_property(n))
            except Exception:pass
        data.append(d)
Path('/Users/olena/unrealtest/demo_reel/spectral_lighting.json').write_text(json.dumps(data,indent=2))
unreal.log('SPECTRAL_LIGHTING_INSPECTED')
