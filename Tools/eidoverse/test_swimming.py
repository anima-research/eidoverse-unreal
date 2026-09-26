"""After test_avatars.py succeeds, exercise live remote swim state transitions.

Uses only the loopback fixture's controls; does not write to production.
"""
import json,time,urllib.request,unreal
from pathlib import Path

swim_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert swim_world
swim_meshes=[c for a in unreal.GameplayStatics.get_all_actors_of_class(swim_world,unreal.Actor)
             for c in a.get_components_by_class(unreal.EidoverseAvatarMesh)]
swim_mesh=min(swim_meshes,key=lambda c:abs(c.get_owner().get_actor_location().y+2400))
swim_phases=[('hover','Hovering',0,16),('swim','Swimming',60,90),
             ('dive','Diving',100,145),('ascend','Ascending',20,60),
             ('surface','Surface swim',0,36),('walk','walk',0,5),
             ('hover','Hovering',0,16)]
swim_index=0
swim_reports=[]

def swim_control(mode):
 request=urllib.request.Request('http://127.0.0.1:18994/step/'+mode,data=b'',method='POST')
 with urllib.request.urlopen(request,timeout=2) as response:assert response.status==200

def swim_tick(dt):
 global swim_index,swim_since,swim_last_lean
 try:
  lean=swim_mesh.body_lean_degrees
  # Rotation targets are smoothed; a mode change must not snap the full body.
  assert abs(lean-swim_last_lean)<35,'body trim snapped'
  swim_last_lean=lean
  if time.monotonic()-swim_since<3:return
  mode,state,low,high=swim_phases[swim_index]
  assert swim_mesh.animation_state==state,(mode,swim_mesh.animation_state)
  assert low<=lean<=high,(mode,lean)
  swim_reports.append({'mode':mode,'state':state,'lean_degrees':lean})
  unreal.log('EIDOVERSE_SWIM_STATE_PASS '+json.dumps(swim_reports[-1]))
  swim_index+=1
  if swim_index==len(swim_phases):
   unreal.unregister_slate_post_tick_callback(swim_handle)
   Path('/Users/olena/unrealtest/eidoverse/swimming_report.json').write_text(json.dumps(swim_reports,indent=2))
   unreal.log('EIDOVERSE_SWIMMING_LIVE_PASS')
   return
  swim_control(swim_phases[swim_index][0]);swim_since=time.monotonic()
 except Exception:
  unreal.unregister_slate_post_tick_callback(swim_handle)
  raise

swim_control(swim_phases[0][0]);swim_since=time.monotonic()
swim_last_lean=swim_mesh.body_lean_degrees
swim_handle=unreal.register_slate_post_tick_callback(swim_tick)
