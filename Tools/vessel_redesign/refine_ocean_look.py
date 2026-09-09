import unreal
from editor_toolset.toolsets.material import MaterialTools as M
MEL=unreal.MaterialEditingLibrary
sun=unreal.load_asset('/Game/Underwater/Materials/LF_SunSpectral')
c=next(e for e in M.get_expressions(sun) if isinstance(e,unreal.MaterialExpressionCustom))
code=c.get_editor_property('code')
code=code.replace('if (depth <= 0.0) return','if (depth <= 0.0 || (OceanEnabled > 0.5 && w.SurfaceDistance(WorldPos)>-120.0)) return')
c.set_editor_property('code',code);MEL.recompile_material(sun);unreal.EditorAssetLibrary.save_loaded_asset(sun)
mat=unreal.load_asset('/Game/Underwater/Materials/M_OceanSurface')
c=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
code=c.get_editor_property('code')
code=code.replace('float foam=smoothstep(46.0,74.0,swell.x)*smoothstep(0.06,0.20,length(gradient))*0.5;','float foam=0.0;')
code=code.replace('float foam=smoothstep(46.0,74.0,swell.x)*smoothstep(0.06,0.20,length(gradient))*0.06;','float foam=0.0;')
c.set_editor_property('code',code);MEL.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('OCEAN_LOOK_REFINED')
