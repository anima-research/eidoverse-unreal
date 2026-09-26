import unreal,json
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
out={}
ocean=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a,unreal.SpectralOcean))
for component in [ocean.surface_mesh,ocean.under_mesh]:
 d={}
 for name in ['render_custom_depth','custom_depth_stencil_value','render_in_main_pass','render_in_depth_pass','visible','hidden_in_game']:
  d[name]=str(component.get_editor_property(name))
 out[component.get_name()]=d
mat=unreal.load_asset('/Game/Underwater/SpectralPrototype/M_SpectralUnderside')
out['material']={n:str(mat.get_editor_property(n)) for n in ['allow_translucent_custom_depth_writes','opacity_mask_clip_value','translucency_pass']}
out['textures']={}
for e in M.get_expressions(unreal.load_asset('/Game/Underwater/SpectralPrototype/M_SpectralOptics')):
 if isinstance(e,unreal.MaterialExpressionSceneTexture):out['textures'][e.get_name()]=str(e.get_editor_property('scene_texture_id'))
Path('/Users/olena/unrealtest/demo_reel/spectral_depth_setup.json').write_text(json.dumps(out,indent=2))
unreal.log('SPECTRAL_DEPTH_SETUP '+json.dumps(out))
