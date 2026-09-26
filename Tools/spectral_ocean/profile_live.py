"""Repeatable PIE A/B, fixed camera, 30 warmup + 90 measured frames per phase.
Frame intervals include editor/Slate costs; these are not standalone GPU timings.
Run without moving or resizing the viewport. No assets are saved.
"""
import unreal,time,json,statistics,os
from pathlib import Path
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
performance_settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
previous_throttle=performance_settings.get_editor_property('bThrottleCPUWhenNotForeground')
performance_settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
ocean=unreal.GameplayStatics.get_actor_of_class(w,unreal.SpectralOcean);assert ocean
pc=unreal.GameplayStatics.get_player_controller(w,0)
pawn=unreal.GameplayStatics.get_player_pawn(w,0);assert pawn
previous_position=pawn.get_actor_location();previous_rotation=pc.get_control_rotation()
pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True)
pc.set_control_rotation(unreal.Rotator(pitch=8,yaw=-148,roll=0))
capture=ocean.get_component_by_class(unreal.SceneCaptureComponentCube)
target=capture.get_editor_property('texture_target')
pp=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PostProcessVolume) if a.actor_has_tag('WaterOpticsPostProcess'))
phases=[('full',True,True),('no_capture',False,True),('no_optics',True,False),('neither',False,False)]
report={'tag':globals().get('profile_run_tag',os.environ.get('SPECTRAL_PROFILE_TAG','before')),'camera':[7900,-2150,-700],'phases':{}}
phase=0;frame=0;samples=[];simulation=[];last=time.perf_counter()
def setup():
 name,cap,fog=phases[phase]
 capture.set_editor_property('texture_target',target if cap else None)
 pp.set_editor_property('blend_weight',1.0 if fog else 0.0)
 unreal.log('SPECTRAL_PROFILE_PHASE '+name)
setup()
def tick(dt):
 global phase,frame,samples,simulation,last
 now=time.perf_counter();elapsed=(now-last)*1000;last=now;frame+=1
 pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True)
 pc.set_control_rotation(unreal.Rotator(pitch=8,yaw=-148,roll=0))
 if frame>30:samples.append(elapsed);simulation.append(ocean.simulation_milliseconds)
 if frame<120:return
 report['phases'][phases[phase][0]]={'median_frame_ms':statistics.median(samples),'mean_frame_ms':statistics.mean(samples),'p95_frame_ms':sorted(samples)[int(len(samples)*.95)],'median_ocean_cpu_ms':statistics.median(simulation),'samples':len(samples)}
 phase+=1;frame=0;samples=[];simulation=[]
 if phase<len(phases):setup();return
 capture.set_editor_property('texture_target',target);pp.set_editor_property('blend_weight',1.0)
 pawn.set_actor_location(previous_position,False,True);pc.set_control_rotation(previous_rotation)
 unreal.unregister_slate_post_tick_callback(profile_handle)
 performance_settings.set_editor_property('bThrottleCPUWhenNotForeground',previous_throttle)
 Path('/Users/olena/unrealtest/demo_reel/spectral_profile_'+report['tag']+'.json').write_text(json.dumps(report,indent=2))
 unreal.log('SPECTRAL_PROFILE_DONE '+json.dumps(report))
profile_handle=unreal.register_slate_post_tick_callback(tick)
