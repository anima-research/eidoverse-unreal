import unreal,json
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
report={}
for name in ('M_OceanSurface','LF_SunSpectral','M_WaterOpticsView'):
    mat=unreal.load_asset('/Game/Underwater/Materials/'+name)
    report[name]=[str(e.get_editor_property('code')) for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom)]
report['water']=[]
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_actor_label()=='WaterSurface':
        c=actor.get_component_by_class(unreal.StaticMeshComponent)
        report['water'].append({'scale':str(actor.get_actor_scale3d()),'mesh':c.static_mesh.get_path_name(),'materials':[str(c.get_material(i)) for i in range(c.get_num_materials())]})
Path('/Users/olena/unrealtest/demo_reel/ocean_inspection.json').write_text(json.dumps(report,indent=2))
unreal.log('OCEAN_INSPECTION_DONE')
