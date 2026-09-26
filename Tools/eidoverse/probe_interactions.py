import unreal, math
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseBridge)[0]
assert b.get_editor_property('endpoint')=='ws://127.0.0.1:19002/ws'
s=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.EidoverseScene)[0]
assert 100<s.get_editor_property('flora_count')<=12000
assert s.get_editor_property('weather_state')=='rain'
assert b.get_editor_property('loaded_avatar_count')>=1
meshes=[]
for actor in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
    meshes.extend(actor.get_components_by_class(unreal.EidoverseAvatarMesh))
target=unreal.Vector(160,65,120)
for m in meshes:
    hand=m.human_bone_position('rightHand')
    shoulder=m.human_bone_position('rightUpperArm')
    elbow=m.human_bone_position('rightLowerArm')
    length=(shoulder-elbow).length()+(elbow-hand).length()
    expected=max(0,(shoulder-target).length()-length)
    gap=(hand-target).length()
    assert abs(gap-expected)<1, (gap,expected)
    unreal.log('EIDOVERSE_REACH_SOLVE_PASS gap_cm='+str(gap)+' clamped_gap_cm='+str(expected))
assert b.get_editor_property('active_physics_objects')==0
b.clear_reach()
pc=unreal.GameplayStatics.get_player_controller(w,0)
pc.set_ignore_look_input(True)
pc.set_control_rotation(unreal.Rotator(pitch=-12,yaw=0,roll=0))
unreal.GameplayStatics.get_player_pawn(w,0).set_actor_location(unreal.Vector(-200,0,100),False,False)
unreal.log('EIDOVERSE_FEATURE_PROBE_PASS plants='+str(s.get_editor_property('flora_count')))
