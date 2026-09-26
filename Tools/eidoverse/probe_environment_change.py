import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseBridge)[0]
assert b.get_editor_property('endpoint')=='ws://127.0.0.1:19002/ws'
s=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseScene)[0]
hour=s.get_editor_property('weather_hour')
lights=[]
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
    if not a.get_editor_property('hidden'):
        lights.extend(a.get_components_by_class(unreal.DirectionalLightComponent))
assert len(lights)==1
assert s.get_editor_property('weather_state')=='clear'
if hour==0:
    assert s.get_editor_property('flora_count')>100
    assert lights[0].get_editor_property('intensity')==0
else:
    assert hour==14
    assert s.get_editor_property('flora_count')==0
    assert lights[0].get_editor_property('intensity')>0
unreal.log('EIDOVERSE_ENVIRONMENT_CHANGE_PASS hour='+str(hour))
