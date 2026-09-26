"""Prototype-only Nanite copies of rigid terrain/rocks/platform; originals intact."""
import unreal,json
from pathlib import Path
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
root='/Game/Underwater/SpectralPrototype'
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(root+'/L_SpectralOcean_Prototype')
assets=unreal.EditorAssetLibrary;meshes=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
names=['ShelfTerrain','ReefRock0','ReefRock1','ReefRock2','ReefRock3','BeddingRock','ResearchPlatform']
mapping={};report=[]
for name in names:
 src='/Game/Underwater/Ecology/Meshes/SM_'+name
 dst=root+'/ShadowMeshes/SM_'+name
 mesh=unreal.load_asset(dst) if assets.does_asset_exist(dst) else assets.duplicate_asset(src,dst)
 assert mesh,dst
 settings=meshes.get_nanite_settings(mesh)
 settings.set_editor_property('enabled',True)
 # Keep the full original fallback for collision and exact non-Nanite A/B.
 settings.set_editor_property('fallback_percent_triangles',1.0)
 settings.set_editor_property('fallback_relative_error',0.0)
 meshes.set_nanite_settings(mesh,settings,True)
 # Persist only the auto-required Nanite usage flag on shared material graphs.
 # The graphs themselves are unchanged; this avoids recompiles each launch/cook.
 for material in mesh.get_editor_property('static_materials'):
  mat=material.get_editor_property('material_interface')
  while isinstance(mat,unreal.MaterialInstance):mat=mat.get_editor_property('parent')
  if mat:
   unreal.MaterialEditingLibrary.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_NANITE)
   assets.save_loaded_asset(mat)
 assets.save_loaded_asset(mesh)
 mapping[src+'.SM_'+name]=mesh
 report.append({'source':src,'prototype':dst,'nanite':meshes.get_nanite_settings(mesh).enabled})
count=0
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
 for c in actor.get_components_by_class(unreal.StaticMeshComponent):
  m=c.get_editor_property('static_mesh')
  if m and m.get_path_name() in mapping:
   c.set_static_mesh(mapping[m.get_path_name()]);count+=1
assert levels.save_current_level()
Path('/Users/olena/unrealtest/demo_reel/nanite_shadow_install.json').write_text(json.dumps({'components_changed':count,'meshes':report},indent=2))
unreal.log('NANITE_SHADOW_INSTALL_DONE '+str(count))
