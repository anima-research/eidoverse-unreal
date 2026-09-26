import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
settings=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
gpu_previous_throttle=settings.get_editor_property('bThrottleCPUWhenNotForeground')
settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
pc=unreal.GameplayStatics.get_player_controller(w,0)
pawn=unreal.GameplayStatics.get_player_pawn(w,0)
pawn.set_actor_location(unreal.Vector(7900,-2150,-700),False,True)
pc.set_control_rotation(unreal.Rotator(pitch=8,yaw=-148,roll=0))
for command in ['r.ProfileGPU.ShowUI 0','r.ProfileGPU.ShowEventHistogram 1','profilegpu']:
 unreal.SystemLibrary.execute_console_command(w,command)
unreal.log('SPECTRAL_GPU_PROFILE_REQUESTED')
