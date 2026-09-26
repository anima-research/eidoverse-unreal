"""Fixed-resolution isolated shadow-cost experiment. No asset saves.
Console values are restored, but restart after profiling to clear priorities.
"""
import unreal,time,json,statistics
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
throttle=settings.get_editor_property('bThrottleCPUWhenNotForeground');settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
pc=unreal.GameplayStatics.get_player_controller(w,0);pawn=unreal.GameplayStatics.get_player_pawn(w,0)
pos=pawn.get_actor_location();rot=pc.get_control_rotation()
names=['r.ScreenPercentage','r.Shadow.Virtual.NonNanite.IncludeInCoarsePages','r.Shadow.Virtual.SMRT.RayCountDirectional']
saved={n:unreal.SystemLibrary.get_console_variable_float_value(n) for n in names}
lights=[c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.DirectionalLight) for c in a.get_components_by_class(unreal.DirectionalLightComponent) if c.get_editor_property('cast_shadows')]
phases=['baseline','no_coarse_non_nanite','half_shadow_rays','no_sun_shadows_diagnostic','baseline_repeat']
report={'viewport':list(pc.get_viewport_size()),'screen_percentage':100,'original':saved,'phases':{}}
phase=0;frame=0;samples=[];last=time.perf_counter()
def command(n,v):unreal.SystemLibrary.execute_console_command(w,n+' '+str(v))
def setup():
 name=phases[phase]
 command(names[0],100)
 command(names[1],0 if name=='no_coarse_non_nanite' else saved[names[1]])
 command(names[2],max(1,int(saved[names[2]])//2) if name=='half_shadow_rays' else saved[names[2]])
 for c in lights:c.set_cast_shadows(name!='no_sun_shadows_diagnostic')
 unreal.log('SHADOW_PROFILE_PHASE '+name)
def tick(dt):
 global phase,frame,samples,last
 now=time.perf_counter();elapsed=(now-last)*1000;last=now;frame+=1
 pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True);pc.set_control_rotation(unreal.Rotator(pitch=8,yaw=-148,roll=0))
 if frame>45:samples.append(elapsed)
 if frame<165:return
 report['phases'][phases[phase]]={'median_ms':statistics.median(samples),'mean_ms':statistics.mean(samples),'p95_ms':sorted(samples)[int(len(samples)*.95)]}
 phase+=1;frame=0;samples=[]
 if phase<len(phases):setup();return
 unreal.unregister_slate_post_tick_callback(handle)
 for n,v in saved.items():command(n,v)
 for c in lights:c.set_cast_shadows(True)
 pawn.set_actor_location(pos,False,True);pc.set_control_rotation(rot)
 settings.set_editor_property('bThrottleCPUWhenNotForeground',throttle)
 Path('/Users/olena/unrealtest/demo_reel/shadow_profile_ab.json').write_text(json.dumps(report,indent=2))
 unreal.log('SHADOW_PROFILE_DONE '+json.dumps(report))
setup();handle=unreal.register_slate_post_tick_callback(tick)
