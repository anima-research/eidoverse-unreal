import os,runpy
os.environ['ECOLOGY_EXPANSION']='1'
try:runpy.run_path('/Users/olena/unrealtest/marine_ecology/import_ecology.py',run_name='__main__')
finally:os.environ.pop('ECOLOGY_EXPANSION',None)
