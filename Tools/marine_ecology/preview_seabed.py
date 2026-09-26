import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
diver=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.actor_has_tag('OceanDiver'))
diver.set_actor_location(unreal.Vector(3600,-3000,-2400),False,True)
unreal.GameplayStatics.get_player_controller(w,0).set_control_rotation(unreal.Rotator(pitch=-32,yaw=45,roll=0))
unreal.log('SEABED_PREVIEW_READY')
