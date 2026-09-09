"""PIE-only regression checks. All test state disappears when PIE stops."""
import unreal
tw=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert tw,'Requires PIE'
tpc=unreal.GameplayStatics.get_player_controller(tw,0)
tsub=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(tw,unreal.Pawn) if a.get_class().get_name()=='BP_Cyclops_C')
tmovement=tsub.get_movement_component()
saved_deceleration=tmovement.get_editor_property('deceleration')
tpc.possess(tsub)
tmovement.set_editor_property('deceleration',0)
stages=[('negative',-20,300,1.5),('positive',20,300,1.5),('wrapped_negative',340,300,1.5),('idle_level',20,0,6),('unoccupied_level',20,0,6)]
state={'i':0,'time':0,'started':False}
def control_test_tick(delta):
 try:
  i=state['i']
  label,angle,speed,seconds=stages[i]
  if not state['started']:
   tsub.set_actor_rotation(unreal.Rotator(pitch=angle if speed==0 else 0,yaw=90,roll=10 if speed==0 else 0),True)
   tpc.set_control_rotation(unreal.Rotator(pitch=angle,yaw=90,roll=0))
   tmovement.set_editor_property('velocity',unreal.Vector(0,speed,0))
   if label=='unoccupied_level':tpc.un_possess()
   state['time']=unreal.GameplayStatics.get_time_seconds(tw);state['started']=True
  if unreal.GameplayStatics.get_time_seconds(tw)-state['time']<seconds:return
  rot=tsub.get_actor_rotation();pitch=unreal.MathLibrary.normalize_axis(rot.pitch)
  if speed:
   expected=-20 if angle in (-20,340) else 20
   assert abs(pitch-expected)<3,(label,pitch,expected)
  else:assert abs(pitch)<2 and abs(rot.roll)<1,(label,rot)
  unreal.log('CONTROL_REGRESSION_PASS '+label+' pitch='+str(pitch)+' roll='+str(rot.roll))
  state['i']+=1;state['started']=False
  if state['i']==len(stages):
   tmovement.set_editor_property('deceleration',saved_deceleration)
   tpc.possess(tsub)
   unreal.unregister_slate_post_tick_callback(control_test_handle)
   unreal.log('CONTROL_REGRESSION_SUCCESS')
 except Exception as e:
  tmovement.set_editor_property('deceleration',saved_deceleration)
  unreal.unregister_slate_post_tick_callback(control_test_handle)
  unreal.log_error('CONTROL_REGRESSION_FAILED '+repr(e))
control_test_handle=unreal.register_slate_post_tick_callback(control_test_tick)
