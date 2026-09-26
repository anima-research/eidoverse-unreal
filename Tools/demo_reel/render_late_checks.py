import os,runpy
os.environ['REEL_MODE']='preview'
os.environ['REEL_OUTPUT']='/Users/olena/unrealtest/demo_reel/checks_late'
os.environ['REEL_SHOTS']='04_benthic,07_surface,08_boarding'
os.environ['REEL_CHECK_FRAME']='165'
try:runpy.run_path(os.path.join(os.path.dirname(__file__),'render_reel.py'),run_name='__main__')
finally:
 for k in ('REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME'):os.environ.pop(k,None)
