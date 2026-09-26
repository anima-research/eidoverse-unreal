"""Short runtime comparison; keep the view still. No persistent world changes."""
import unreal,time,json,statistics
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws'
pp=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PostProcessVolume) if a.get_editor_property('priority')==100)
unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings').set_editor_property('bThrottleCPUWhenNotForeground',False)
unreal.SystemLibrary.execute_console_command(None,'Slate.bAllowThrottling 0')
modes=['screen_space','lumen_hit','lumen_reflections_hit']
samples=[];results={};stage=0;started=time.monotonic()
def set_mode(mode):
    s=pp.get_editor_property('settings')
    s.set_editor_property('dynamic_global_illumination_method',unreal.DynamicGlobalIlluminationMethod.SCREEN_SPACE if mode=='screen_space' else unreal.DynamicGlobalIlluminationMethod.LUMEN)
    s.set_editor_property('reflection_method',unreal.ReflectionMethod.SCREEN_SPACE if mode=='screen_space' else unreal.ReflectionMethod.LUMEN)
    s.set_editor_property('lumen_ray_lighting_mode',unreal.LumenRayLightingModeOverride.HIT_LIGHTING_FOR_REFLECTIONS if mode=='lumen_reflections_hit' else unreal.LumenRayLightingModeOverride.HIT_LIGHTING)
    pp.set_editor_property('settings',s)
def profile_tick(dt):
    global stage,started,samples
    elapsed=time.monotonic()-started
    if elapsed>2:samples.append(unreal.GameplayStatics.get_world_delta_seconds(w)*1000)
    if elapsed<7:return
    ordered=sorted(samples)
    results[modes[stage]]={'mean_ms':statistics.mean(samples),'p95_ms':ordered[int(.95*(len(ordered)-1))],'frames':len(samples)}
    stage+=1
    if stage==len(modes):
        unreal.unregister_slate_post_tick_callback(handle)
        set_mode('lumen_hit')
        unreal.log('COMMONS_LUMEN_PROFILE '+json.dumps(results))
        return
    samples=[];started=time.monotonic();set_mode(modes[stage])
set_mode(modes[0])
handle=unreal.register_slate_post_tick_callback(profile_tick)
