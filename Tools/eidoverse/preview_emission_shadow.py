"""Temporary local-only shadow test; run restore_emission_shadow.py afterwards.
No server verbs, saved assets, or permanent level edits.
"""
import unreal
assert not getattr(unreal,'_mythos_emission_qa',None),'Restore the previous test first'
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws'
m=next(c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
       for c in a.get_components_by_class(unreal.EidoverseAvatarMesh) if c.get_num_bones()==445)
lamp=m.emissive_lights[0]
pawn=unreal.GameplayStatics.get_player_pawn(w,0)
pc=unreal.GameplayStatics.get_player_controller(w,0)
qa={'bridge':b,'tick':b.is_actor_tick_enabled(),'lights':[],'props':[],
    'pawn':pawn,'pos':pawn.get_actor_location(),'rotation':pc.get_control_rotation(),'pc':pc,
    'lamp':lamp,'lamp_intensity':lamp.get_editor_property('intensity'),
    'lamp_shadow':lamp.get_editor_property('cast_shadows')}
unreal._mythos_emission_qa=qa
b.set_actor_tick_enabled(False)
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
    if a.get_editor_property('hidden'):continue
    for c in a.get_components_by_class(unreal.LightComponentBase):
        qa['lights'].append((c,c.get_editor_property('visible')))
        c.set_visibility(c==lamp)
lamp.set_intensity(13.8*.45) # Authored night peak, not an exaggerated test light.
lamp.set_cast_shadows(True)
origin=lamp.get_world_location()
camera=unreal.GameplayStatics.get_player_camera_manager(w,0)
eye_offset=camera.get_camera_location()-pawn.get_actor_location()
eye=origin+unreal.Vector(210,-120,15)
pawn.set_actor_location(eye-eye_offset,False,True)
pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye,origin+unreal.Vector(0,0,-45)))
unreal.log('MYTHOS_SHADOW_TEST_READY: isolated authored chest light; existing terrain/props receive shadows')
