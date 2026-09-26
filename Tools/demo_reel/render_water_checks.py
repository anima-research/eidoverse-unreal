"""Repeatable water-regression views. WATER_CHECK_PASS selects a fresh directory."""
import os,runpy
from pathlib import Path
keys=('REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME')
saved={k:os.environ.get(k) for k in keys}
try:
    os.environ['REEL_MODE']='preview'
    os.environ['REEL_OUTPUT']='/Users/olena/unrealtest/demo_reel/water_checks_'+os.environ.get('WATER_CHECK_PASS','02')
    os.environ['REEL_SHOTS']='07_surface,09_platform'
    # Platform only lasts 180 frames; use 150 for a shared late, above-water view.
    os.environ['REEL_CHECK_FRAME']='150'
    runpy.run_path(str(Path(__file__).with_name('render_reel.py')),run_name='__main__')
finally:
    for key,value in saved.items():
        if value is None:os.environ.pop(key,None)
        else:os.environ[key]=value
