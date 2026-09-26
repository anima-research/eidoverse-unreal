"""Read-only shadow caster inventory for the prototype."""
import unreal,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype')
report={'casters':[],'cvars':{}}
for name in ['r.Nanite','r.Shadow.Virtual.Enable','r.Shadow.Virtual.NonNanite.IncludeInCoarsePages','r.Shadow.Virtual.MarkCoarsePagesDirectional','r.Shadow.Virtual.MarkCoarsePagesLocal','r.Shadow.Virtual.SMRT.RayCountDirectional']:
 report['cvars'][name]=unreal.SystemLibrary.get_console_variable_int_value(name)
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 for c in a.get_components_by_class(unreal.StaticMeshComponent):
  if not c.get_editor_property('cast_shadow'):continue
  m=c.get_editor_property('static_mesh')
  if not m:continue
  d={'actor':a.get_actor_label(),'component':c.get_name(),'mesh':m.get_path_name(),'hidden':a.get_editor_property('hidden'),'mobility':str(c.get_editor_property('mobility')),'nanite':m.get_editor_property('nanite_settings').get_editor_property('enabled'),'bounds':str(c.get_local_bounds())}
  if isinstance(c,unreal.InstancedStaticMeshComponent):d['instances']=c.get_instance_count()
  d['materials']=[str(x) for x in c.get_materials()]
  report['casters'].append(d)
Path('/Users/olena/unrealtest/demo_reel/shadow_inventory.json').write_text(json.dumps(report,indent=2))
unreal.log('SHADOW_INVENTORY_DONE '+str(len(report['casters'])))
