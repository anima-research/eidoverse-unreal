import unreal,os,runpy
from pathlib import Path
from editor_toolset.toolsets.material import MaterialTools as M
mat=unreal.load_asset('/Game/Underwater/SpectralPrototype/M_SpectralOptics')
c=next(e for e in M.get_expressions(mat) if isinstance(e,unreal.MaterialExpressionCustom))
c.set_editor_property('code','return float3(saturate(CDepth.r/10000.0),saturate(SDepth.r/10000.0),saturate(CStencil.r/4.0));\n'+c.get_editor_property('code'))
M.recompile(mat)
here=Path(__file__).resolve().parent
os.environ.update(REEL_MAP='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype',REEL_MODE='preview',REEL_OUTPUT='/Users/olena/unrealtest/demo_reel/spectral_depth_debug',REEL_SHOTS='10_underwater',REEL_CHECK_FRAME='90',REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_RECIPE=str(here/'shots_spectral.json'))
runpy.run_path(str(here.parent/'demo_reel/render_reel.py'),run_name='__main__')
for key in ['REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME','REEL_ROOT','REEL_RECIPE']:os.environ.pop(key,None)
