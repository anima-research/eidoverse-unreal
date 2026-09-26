import unreal,json
from pathlib import Path
scope_names=['r.TranslucencyLightingVolume','r.Shadow.Virtual.MaxPhysicalPages']
def values():return [unreal.SystemLibrary.get_console_variable_int_value(n) for n in scope_names]
scope_report={'before':values()};scope_phase=0;scope_frames=0
def check_scope(dt):
 global scope_phase,scope_frames
 w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 if scope_phase==0 and w:
  scope_frames+=1
  if scope_frames<15:return
  scope_report['during']=values()
  assert scope_report['during']==[0,max(8192,scope_report['before'][1])],scope_report
  scope_phase=1
  unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
 elif scope_phase==1 and not w:
  unreal.unregister_slate_post_tick_callback(scope_handle)
  scope_report['after']=values();assert scope_report['before']==scope_report['after'],scope_report
  Path('/Users/olena/unrealtest/demo_reel/performance_scope_validation.json').write_text(json.dumps(scope_report,indent=2))
  unreal.log('PERFORMANCE_SCOPE_PASS '+json.dumps(scope_report))
scope_handle=unreal.register_slate_post_tick_callback(check_scope)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
