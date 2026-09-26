import os,runpy
from pathlib import Path
os.environ['SPECTRAL_PROFILE_TAG']='after'
try:runpy.run_path(str(Path(__file__).with_name('start_profile.py')),run_name='__main__')
finally:os.environ.pop('SPECTRAL_PROFILE_TAG',None)
