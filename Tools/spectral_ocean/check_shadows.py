"""Matched MRQ angles. SHADOW_QA_VARIANT controls fresh output folder name."""
import unreal,os,runpy
from pathlib import Path
here=Path(__file__).resolve().parent
variant=os.environ.get('SHADOW_QA_VARIANT','optimized')
keys=('REEL_RECIPE','REEL_ROOT','REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME')
saved={k:os.environ.get(k) for k in keys}
try:
 os.environ.update(REEL_RECIPE=str(here/'shots_lighting.json'),REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_MAP='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype',REEL_MODE='preview',REEL_OUTPUT='/Users/olena/unrealtest/demo_reel/shadow_quality_'+variant,REEL_SHOTS='',REEL_CHECK_FRAME='90')
 runpy.run_path(str(here.parent/'demo_reel/render_reel.py'),run_name='__main__')
finally:
 for k,v in saved.items():
  if v is None:os.environ.pop(k,None)
  else:os.environ[k]=v
