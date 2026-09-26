"""ABBA runtime comparison against full-detail fallback meshes, fixed resolution."""
import unreal,time,json,statistics
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
throttle=settings.get_editor_property('bThrottleCPUWhenNotForeground');settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
screen=unreal.SystemLibrary.get_console_variable_float_value('r.ScreenPercentage')
unreal.SystemLibrary.execute_console_command(w,'r.ScreenPercentage 100')
gpu_names=['r.ProfileGPU.ShowUI','r.ProfileGPU.ShowEventHistogram','r.RDG.AsyncCompute','Slate.bAllowThrottling','t.IdleWhenNotForeground']
gpu_saved={n:unreal.SystemLibrary.get_console_variable_int_value(n) for n in gpu_names}
for n,v in zip(gpu_names,[0,1,0,0,0]):unreal.SystemLibrary.execute_console_command(w,n+' '+str(v))
pc=unreal.GameplayStatics.get_player_controller(w,0);pawn=unreal.GameplayStatics.get_player_pawn(w,0)
pos=pawn.get_actor_location();rot=pc.get_control_rotation()
components=[c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor) for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.get_editor_property('static_mesh') and '/SpectralPrototype/ShadowMeshes/' in c.get_editor_property('static_mesh').get_path_name()]
assert len({c.get_editor_property('static_mesh').get_path_name() for c in components})==7,len(components)
phases=['fallback_a','nanite_a','nanite_b','fallback_b']
report={'viewport':list(pc.get_viewport_size()),'screen_percentage':100,'components':len(components),'phases':{}}
phase=0;frame=0;samples=[];last=time.perf_counter()
def setup():
 for c in components:c.set_force_disable_nanite(phases[phase].startswith('fallback'))
 unreal.log('NANITE_PROFILE_PHASE '+phases[phase])
def tick(dt):
 global phase,frame,samples,last
 now=time.perf_counter();elapsed=(now-last)*1000;last=now;frame+=1
 pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True);pc.set_control_rotation(unreal.Rotator(pitch=8,yaw=-148,roll=0))
 if 45<frame<145:samples.append(elapsed)
 if frame==145:
  unreal.log('NANITE_GPU_CAPTURE '+phases[phase])
  unreal.SystemLibrary.execute_console_command(w,'profilegpu')
 if frame<165:return
 report['phases'][phases[phase]]={'median_ms':statistics.median(samples),'mean_ms':statistics.mean(samples),'p95_ms':sorted(samples)[int(len(samples)*.95)]}
 phase+=1;frame=0;samples=[]
 if phase<len(phases):setup();return
 unreal.unregister_slate_post_tick_callback(handle)
 for c in components:c.set_force_disable_nanite(False)
 pawn.set_actor_location(pos,False,True);pc.set_control_rotation(rot)
 unreal.SystemLibrary.execute_console_command(w,'r.ScreenPercentage '+str(screen))
 for n,v in gpu_saved.items():unreal.SystemLibrary.execute_console_command(w,n+' '+str(v))
 settings.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
 Path('/Users/olena/unrealtest/demo_reel/nanite_shadow_profile_abba.json').write_text(json.dumps(report,indent=2))
 unreal.log('NANITE_PROFILE_DONE '+json.dumps(report))
setup();handle=unreal.register_slate_post_tick_callback(tick)
