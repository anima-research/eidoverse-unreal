import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if w:
    b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
    assert b and b.endpoint=='ws://127.0.0.1:19002/ws'
    b.set_share_scene_screenshots(False)
    b.close_chat()
    b.disconnect()
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings').set_editor_property('bThrottleCPUWhenNotForeground',True)
unreal.SystemLibrary.execute_console_command(None,'Slate.bAllowThrottling 1')
unreal.log('DYNAMIC_QA_FINISHED: disconnected, consent off, end PIE requested, normal foreground throttling restored')
