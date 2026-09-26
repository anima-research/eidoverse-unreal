"""Underwater reflection uses the captured submerged scene, not the air skylight."""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
mat=unreal.load_asset('/Game/Underwater/SpectralPrototype/M_SpectralUnderside')
mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property('screen_space_reflections',False)
M.recompile(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('SPECTRAL_UNDERSIDE_TUNED')
