"""Do not integrate underwater fog over a visible ocean interface viewed from air.

Rasterization already found the first opaque wave surface. Re-intersecting the
ideal analytic ocean on this air-only ray creates phantom water segments and
regular grazing-angle bands, especially where the render mesh gets coarser.
Underwater cameras and foreground vessels retain their existing optical paths.
"""
import unreal
from editor_toolset.toolsets.material import MaterialTools as M
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
mat=unreal.load_asset('/Game/Underwater/Materials/M_WaterOpticsView')
custom=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
code=custom.get_editor_property('code')
marker='// AIR_TO_VISIBLE_OCEAN_INTERFACE'
if marker not in code:
    code=code.replace('float3 endpoint=WorldPos;', '''
// AIR_TO_VISIBLE_OCEAN_INTERFACE
bool oceanIsFirst=abs(CStencil.r-1.0)<0.1 && CDepth.r>0.0
    && CDepth.r<=SDepth.r+max(0.5,SDepth.r*0.00002);
if(OceanEnabled>0.5 && oceanIsFirst && w.SurfaceDistance(CamPos)>0.0)
    return SceneColor.rgb;
float3 endpoint=WorldPos;''')
    custom.set_editor_property('code',code)
M.recompile(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('OCEAN_INTERFACE_FOG_FIX_SUCCESS')
