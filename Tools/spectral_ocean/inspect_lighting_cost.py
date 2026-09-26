import unreal,json
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not levels.is_in_play_in_editor()
assert levels.load_level('/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype')
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
report={'lights':[],'materials':{}}
for a in actors:
 for c in a.get_components_by_class(unreal.LightComponent):
  d={'actor':a.get_actor_label(),'class':c.get_class().get_name()}
  for p in ['intensity','cast_shadows','cast_dynamic_shadows','affect_translucent_lighting','light_function_material','light_color','source_angle','specular_scale','indirect_lighting_intensity']:
   try:d[p]=str(c.get_editor_property(p))
   except Exception:pass
  report['lights'].append(d)
 for c in a.get_components_by_class(unreal.MeshComponent):
  for m in c.get_materials():
   if not m:continue
   while isinstance(m,unreal.MaterialInstance):m=m.get_editor_property('parent')
   if not m or m.get_path_name() in report['materials']:continue
   d={}
   for p in ['blend_mode','shading_model','translucency_lighting_mode','allow_front_layer_translucency']:
    try:d[p]=str(m.get_editor_property(p))
    except Exception:pass
   report['materials'][m.get_path_name()]=d
m=unreal.load_asset('/Game/Underwater/Materials/LF_SunSpectral')
report['sun_code']=[e.get_editor_property('code') for e in M.get_expressions(m) if isinstance(e,unreal.MaterialExpressionCustom)]
Path('/Users/olena/unrealtest/demo_reel/spectral_lighting_cost.json').write_text(json.dumps(report,indent=2))
unreal.log('LIGHTING_COST_INSPECT_DONE')
