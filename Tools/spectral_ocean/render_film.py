import os,runpy
from pathlib import Path
here=Path(__file__).resolve().parent
keys=('REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_ROOT','REEL_RECIPE')
saved={k:os.environ.get(k) for k in keys}
try:
    os.environ.update(REEL_MAP='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype',REEL_MODE='final',REEL_OUTPUT='/Users/olena/unrealtest/demo_reel/spectral_film_02',REEL_SHOTS='',REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_RECIPE=str(here/'shots_spectral.json'))
    runpy.run_path(str(here.parent/'demo_reel/render_reel.py'),run_name='__main__')
finally:
    for k,v in saved.items():
        if v is None:os.environ.pop(k,None)
        else:os.environ[k]=v
