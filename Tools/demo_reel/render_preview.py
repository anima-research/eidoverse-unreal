import os,runpy
os.environ['REEL_MODE']='preview'
runpy.run_path(os.path.join(os.path.dirname(__file__),'render_reel.py'),run_name='__main__')
