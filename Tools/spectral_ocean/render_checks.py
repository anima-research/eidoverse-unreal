import os,runpy,unreal,json
from pathlib import Path
here=Path(__file__).resolve().parent
output='/Users/olena/unrealtest/demo_reel/spectral_checks_09'
keys=('REEL_MAP','REEL_MODE','REEL_OUTPUT','REEL_SHOTS','REEL_CHECK_FRAME','REEL_ROOT','REEL_RECIPE')
saved={k:os.environ.get(k) for k in keys}
def check_runtime(dt):
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:return
    found=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.SpectralOcean)
    if not found:return
    actor=found[0]
    if actor.get_editor_property('update_count')<10:return
    report={n:str(actor.get_editor_property(n)) for n in ('simulation_milliseconds','camera_depth','update_count')}
    report['field_count']=len(actor.get_editor_property('fields'))
    report['height']=actor.get_surface_height(unreal.Vector(0,6000,0))
    Path(output).mkdir(parents=True,exist_ok=True)
    Path(output,'runtime.json').write_text(json.dumps(report,indent=2))
    unreal.log('SPECTRAL_RUNTIME '+json.dumps(report))
    unreal.unregister_slate_post_tick_callback(spectral_check_handle)
spectral_check_handle=unreal.register_slate_post_tick_callback(check_runtime)
try:
    os.environ.update(REEL_MAP='/Game/Underwater/SpectralPrototype/L_SpectralOcean_Prototype',REEL_MODE='preview',REEL_OUTPUT=output,REEL_SHOTS='',REEL_CHECK_FRAME='90',REEL_ROOT='/Game/Underwater/SpectralPrototype/Cinematics',REEL_RECIPE=str(here/'shots_spectral.json'))
    runpy.run_path(str(here.parent/'demo_reel/render_reel.py'),run_name='__main__')
finally:
    for k,v in saved.items():
        if v is None:os.environ.pop(k,None)
        else:os.environ[k]=v
