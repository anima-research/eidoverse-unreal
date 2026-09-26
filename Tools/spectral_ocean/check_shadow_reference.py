import os,runpy
from pathlib import Path
previous=os.environ.get('SHADOW_QA_VARIANT')
try:
 os.environ['SHADOW_QA_VARIANT']='reference'
 runpy.run_path(str(Path(__file__).with_name('check_shadows.py')),run_name='__main__')
finally:
 if previous is None:os.environ.pop('SHADOW_QA_VARIANT',None)
 else:os.environ['SHADOW_QA_VARIANT']=previous
