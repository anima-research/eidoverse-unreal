"""Matched MRQ quality checks with TLV enabled/disabled; no map-setting writes."""
import unreal,json,os,runpy
from pathlib import Path
here=Path(__file__).resolve().parent
recipe=json.loads((here.parent/'demo_reel/shots.json').read_text())
recipe['map']='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype'
recipe['shots']=[s for s in recipe['shots'] if s['id'] in ('01_habitat','06_submarine','07_surface','09_platform')]
recipe['shots']+= [s for s in json.loads((here/'shots_spectral.json').read_text())['shots'] if s['id']=='10_underwater']
path=here/'shots_lighting.json';path.write_text(json.dumps(recipe,indent=2))
variant=os.environ.get('LIGHTING_QA_VARIANT','optimized')
keys=('REEL_RECIPE','REEL_ROOT','REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME')
saved={k:os.environ.get(k) for k in keys}
try:
 os.environ.update(REEL_RECIPE=str(path),REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_MAP=recipe['map'],REEL_MODE='preview',REEL_OUTPUT='/Users/olena/unrealtest/demo_reel/lighting_quality_'+variant,REEL_SHOTS='',REEL_CHECK_FRAME='90')
 runpy.run_path(str(here.parent/'demo_reel/build_reel.py'),run_name='__main__')
 # MRQ owns/restores its console override. The native scoped override remains
 # underneath it, and is removed by EndPlay at the end of every render world.
 source=(here.parent/'demo_reel/render_reel.py').read_text()
 source=source.replace("'t.MaxFPS':0.0", "'t.MaxFPS':0.0,'r.TranslucencyLightingVolume':"+('1.0' if variant=='reference' else '0.0'))
 exec(compile(source,str(here.parent/'demo_reel/render_reel.py'),'exec'),{'__file__':str(here.parent/'demo_reel/render_reel.py'),'__name__':'__main__'})
finally:
 for k,v in saved.items():
  if v is None:os.environ.pop(k,None)
  else:os.environ[k]=v
