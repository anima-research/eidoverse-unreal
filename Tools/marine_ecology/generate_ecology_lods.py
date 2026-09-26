"""Conservative geometric LODs for dense flora and small bottom-life clusters."""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
sub=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
for name in ('GiantKelp0','GiantKelp1','GiantKelp2','AnemoneCluster','SpongeCluster','Crab','SeaCucumber','MusselCluster','Urchin','ShellHash','SeaStar'):
 mesh=unreal.load_asset('/Game/Underwater/Ecology/Meshes/SM_'+name)
 options=unreal.StaticMeshReductionOptions()
 options.set_editor_property('auto_compute_lod_screen_size',False)
 settings=[]
 for percent,screen in [(1,1),(.65,.035),(.3,.012)]:
  s=unreal.StaticMeshReductionSettings();s.set_editor_property('percent_triangles',percent);s.set_editor_property('screen_size',screen);settings.append(s)
 options.set_editor_property('reduction_settings',settings)
 assert sub.set_lods(mesh,options)==3
 unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 unreal.log('ECOLOGY_LOD_OK '+name)
unreal.log('ECOLOGY_LODS_SUCCESS')
