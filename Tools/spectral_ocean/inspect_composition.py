import unreal,json
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
out={}
for name in ['M_SpectralUnderside','M_SpectralOptics']:
 m=unreal.load_asset('/Game/Underwater/SpectralPrototype/'+name)
 d={}
 for p in ['blendable_location','translucency_pass','disable_depth_test','apply_fogging','compute_fog_per_pixel','blend_mode']:
  try:d[p]=str(m.get_editor_property(p))
  except Exception:pass
 d['code']=next(e for e in M.get_expressions(m) if isinstance(e,unreal.MaterialExpressionCustom)).get_editor_property('code')
 out[name]=d
Path('/Users/olena/unrealtest/demo_reel/spectral_composition.json').write_text(json.dumps(out,indent=2))
unreal.log('COMPOSITION_INSPECT_DONE')
