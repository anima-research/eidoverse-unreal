import os,runpy
from pathlib import Path
os.environ['LIGHTING_QA_VARIANT']='reference'
try:runpy.run_path(str(Path(__file__).with_name('check_lighting_quality.py')),run_name='__main__')
finally:os.environ.pop('LIGHTING_QA_VARIANT',None)
