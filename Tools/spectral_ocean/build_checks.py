import os,runpy,json
from pathlib import Path
here=Path(__file__).resolve().parent
recipe=json.loads((here.parent/'demo_reel/shots.json').read_text())
recipe['map']='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype'
recipe['shots']=[s for s in recipe['shots'] if s['id'] in ('07_surface','09_platform')]
recipe['shots'].append({'id':'10_underwater','seconds':6,'focal':14,'title':'Looking through the surface','caption':'Coherent wave geometry and underwater refraction.','camera':[[0,[1600,4500,-450],[1600,4500,600]],[6,[1900,4300,-250],[1900,4300,1000]]]})
recipe['shots'].append({'id':'11_wave_detail','seconds':6,'focal':28,'title':'At the waterline','caption':'Actual vertical and horizontal displacement, not scrolling normal layers.','camera':[[0,[2200,2500,120],[1400,6500,80]],[6,[2500,2900,130],[1600,7000,90]]]})
(here/'shots_spectral.json').write_text(json.dumps(recipe,indent=2))
os.environ['REEL_RECIPE']=str(here/'shots_spectral.json')
os.environ['REEL_ROOT']='/Game/Underwater/SpectralPrototype/Cinematics'
try:runpy.run_path(str(here.parent/'demo_reel/build_reel.py'),run_name='__main__')
finally:
    os.environ.pop('REEL_RECIPE',None);os.environ.pop('REEL_ROOT',None)
