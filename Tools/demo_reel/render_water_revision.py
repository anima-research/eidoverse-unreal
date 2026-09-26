"""Full-resolution surface/boarding/platform cuts for the water revision."""
import os,runpy
from pathlib import Path
keys=('REEL_MODE','REEL_OUTPUT','REEL_SHOTS')
saved={k:os.environ.get(k) for k in keys}
try:
    os.environ['REEL_MODE']='final'
    os.environ['REEL_OUTPUT']='/Users/olena/unrealtest/demo_reel/water_revision_02'
    os.environ['REEL_SHOTS']='07_surface,08_boarding,09_platform'
    runpy.run_path(str(Path(__file__).with_name('render_reel.py')),run_name='__main__')
finally:
    for key,value in saved.items():
        if value is None:os.environ.pop(key,None)
        else:os.environ[key]=value
