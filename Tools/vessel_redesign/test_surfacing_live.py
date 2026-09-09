"""PIE swim-input regression: rise, float, dive for both existing pawn types."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
pc=unreal.GameplayStatics.get_player_controller(w,0)
oc=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.WaterOpticsController)[0]
pawns=[next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.get_class().get_name()==name) for name in ('BP_DiverPawn_C','BP_Cyclops_C')]
st={'pawn':0,'phase':'start','time':0,'samples':[]}
def tick_surface(dt):
 try:
  p=pawns[st['pawn']];now=unreal.GameplayStatics.get_time_seconds(w)
  phase=st['phase'];pos=p.get_actor_location()
  if phase=='start':
   pc.possess(p);pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=90,roll=0))
   p.set_actor_location(unreal.Vector(10000+st['pawn']*3000,-6500,-650),False,True)
   p.get_movement_component().stop_movement_immediately()
   st.update(phase='rise',time=now,samples=[])
  elif phase=='rise':
   p.add_movement_input(unreal.Vector(0,0,1),1,True)
   if pos.z-oc.get_ocean_height(pos)>15:
    unreal.log('SURFACING_PASS '+p.get_class().get_name()+' reached surface using upward movement')
    st.update(phase='float',time=now)
   elif now-st['time']>14:raise AssertionError(('ascent timeout',pos))
  elif phase=='float':
   if now-st['time']>1:st['samples'].append(pos.z-oc.get_ocean_height(pos))
   if now-st['time']>4:
    assert min(st['samples'])>0 and max(st['samples'])<90,st['samples']
    assert abs(p.get_velocity().z)<1,p.get_velocity()
    unreal.log('SURFACING_PASS '+p.get_class().get_name()+' floats above waves '+str((min(st['samples']),max(st['samples']))))
    st.update(phase='dive',time=now)
  elif phase=='dive':
   p.add_movement_input(unreal.Vector(0,0,-1),1,True)
   if pos.z-oc.get_ocean_height(pos)<-260:
    unreal.log('SURFACING_PASS '+p.get_class().get_name()+' dives back below waves')
    st['pawn']+=1;st['phase']='start'
    if st['pawn']==len(pawns):
     unreal.unregister_slate_post_tick_callback(surfacing_handle)
     p.get_movement_component().stop_movement_immediately()
     unreal.log('SURFACING_REGRESSION_SUCCESS')
   elif now-st['time']>10:raise AssertionError(('dive timeout',pos))
 except Exception as e:
  unreal.unregister_slate_post_tick_callback(surfacing_handle)
  unreal.log_error('SURFACING_REGRESSION_FAILED '+repr(e))
surfacing_handle=unreal.register_slate_post_tick_callback(tick_surface)
