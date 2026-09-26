import os,runpy
os.environ['REEL_MODE']='preview'
os.environ['REEL_OUTPUT']='/Users/olena/unrealtest/demo_reel/checks_v2'
os.environ['REEL_SHOTS']='01_habitat,04_benthic,07_surface,08_boarding'
try:runpy.run_path(os.path.join(os.path.dirname(__file__),'render_reel.py'),run_name='__main__')
finally:
 os.environ.pop('REEL_OUTPUT',None);os.environ.pop('REEL_SHOTS',None)
