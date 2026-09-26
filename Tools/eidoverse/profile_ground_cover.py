"""Local-only same-view cover/no-cover frame-time comparison; restores visibility.
Run with a still camera, shaders/models loaded, and foreground throttling disabled.
This measures editor frame time, not isolated GPU time or a packaged benchmark.
"""
import unreal, time, statistics, json

def start_cover_profile():
    assert not getattr(unreal,'_cover_profile_active',False),'A profile is already running'
    w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    bridge=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
    assert bridge.joined and bridge.endpoint=='ws://127.0.0.1:19002/ws'
    scene=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
    assert scene.loaded_models==70 and scene.failed_models==0
    cover=[c for c in scene.get_components_by_class(unreal.PrimitiveComponent)
           if isinstance(c,unreal.ProceduralMeshComponent) and c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
           or isinstance(c,unreal.HierarchicalInstancedStaticMeshComponent)]
    assert cover
    original=[(c,c.get_editor_property('visible')) for c in cover]
    stages=[True,False,True]
    state={'stage':0,'start':time.monotonic(),'samples':[],'result':[]}
    def tick(dt):
        if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()!=w:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal._cover_profile_active=False
            return
        elapsed=time.monotonic()-state['start']
        if elapsed>2:state['samples'].append(unreal.GameplayStatics.get_world_delta_seconds(w)*1000)
        if elapsed<7:return
        values=sorted(state['samples'])
        state['result'].append({'cover':stages[state['stage']], 'frames':len(values),
            'median_ms':statistics.median(values),'p95_ms':values[int(.95*(len(values)-1))]})
        state['stage']+=1
        if state['stage']==len(stages):
            unreal.unregister_slate_post_tick_callback(state['handle'])
            unreal._cover_profile_active=False
            for c,visible in original:c.set_visibility(visible)
            unreal.log('GROUND_COVER_PROFILE '+json.dumps(state['result']))
            return
        for c in cover:c.set_visibility(stages[state['stage']])
        state['samples']=[];state['start']=time.monotonic()
    state['handle']=unreal.register_slate_post_tick_callback(tick)
    unreal._cover_profile_active=True
start_cover_profile()
