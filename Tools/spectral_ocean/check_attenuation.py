"""Fixed-camera fog regression at 3 m, 30 m and 90 m depth, plus above water."""
import os,runpy,json
from pathlib import Path
here=Path(__file__).resolve().parent
recipe=json.loads((here/'shots_spectral.json').read_text())
recipe['shots']=[s for s in recipe['shots'] if s['id']=='09_platform']
for key,z in [('12_shallow',-300),('13_deep',-3000),('14_abyss',-9000)]:
 recipe['shots'].append(dict(id=key,seconds=2,focal=18,title=key,caption='',camera=[[0,[7900,-2150,z],[2000,-6000,z+2500]],[2,[7900,-2150,z],[2000,-6000,z+2500]]]))
path=here/'shots_attenuation.json';path.write_text(json.dumps(recipe,indent=2))
keys=('REEL_RECIPE','REEL_ROOT','REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME')
saved={k:os.environ.get(k) for k in keys}
try:
 os.environ.update(REEL_RECIPE=str(path),REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_MAP=recipe['map'],REEL_MODE='preview',REEL_OUTPUT='/Users/olena/unrealtest/demo_reel/spectral_attenuation_'+os.environ.get('SPECTRAL_QA_TAG','after_03'),REEL_SHOTS='',REEL_CHECK_FRAME='30')
 runpy.run_path(str(here.parent/'demo_reel/build_reel.py'),run_name='__main__')
 runpy.run_path(str(here.parent/'demo_reel/render_reel.py'),run_name='__main__')
finally:
 for k,v in saved.items():
  if v is None:os.environ.pop(k,None)
  else:os.environ[k]=v
