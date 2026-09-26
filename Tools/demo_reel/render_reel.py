"""Render shots as independent MRQ jobs. Environment: REEL_MODE, REEL_SHOTS,
REEL_OUTPUT. Default is a low-cost preview; use REEL_MODE=final for full frames.
"""
import unreal,json,os
HERE=os.path.dirname(__file__)
recipe=json.load(open(os.environ.get('REEL_RECIPE',os.path.join(HERE,'shots.json'))))
mode=os.environ.get('REEL_MODE','preview')
output=os.environ.get('REEL_OUTPUT','/Users/olena/unrealtest/demo_reel/'+mode)
selected=os.environ.get('REEL_SHOTS','').split(',')
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
subsystem=unreal.get_editor_subsystem(unreal.MoviePipelineQueueSubsystem)
# Use a private queue; preserve any queue the user had open.
reel_queue=unreal.MoviePipelineQueue()
for shot in recipe['shots']:
 if selected!=[''] and shot['id'] not in selected:continue
 job=reel_queue.allocate_new_job(unreal.MoviePipelineExecutorJob)
 job.job_name=shot['id'];job.map=unreal.SoftObjectPath(os.environ.get('REEL_MAP',recipe['map']))
 job.sequence=unreal.SoftObjectPath(os.environ.get('REEL_ROOT','/Game/Underwater/Cinematics/DemoReel')+'/LS_'+shot['id'])
 cfg=job.get_configuration()
 out=cfg.find_or_add_setting_by_class(unreal.MoviePipelineOutputSetting)
 os.makedirs(os.path.join(output,shot['id']),exist_ok=True)
 out.output_directory=unreal.DirectoryPath(os.path.join(output,shot['id']))
 out.file_name_format='{frame_number_rel}';out.zero_pad_frame_numbers=5
 out.output_resolution=unreal.IntPoint(*(recipe['resolution'] if mode=='final' else [960,540]))
 out.use_custom_frame_rate=True;out.output_frame_rate=unreal.FrameRate(recipe['fps'],1)
 out.override_existing_output=False
 out.flush_disk_writes_per_shot=True
 if mode=='preview':
  frame=int(os.environ.get('REEL_CHECK_FRAME','45'))
  out.use_custom_playback_range=True;out.custom_start_frame=frame;out.custom_end_frame=frame+1
 render=cfg.find_or_add_setting_by_class(unreal.MoviePipelineDeferredPassBase)
 render.disable_multisample_effects=False
 cfg.find_or_add_setting_by_class(unreal.MoviePipelineImageSequenceOutput_PNG)
 aa=cfg.find_or_add_setting_by_class(unreal.MoviePipelineAntiAliasingSetting)
 aa.spatial_sample_count=1;aa.temporal_sample_count=1
 aa.engine_warm_up_count=60;aa.render_warm_up_count=24;aa.render_warm_up_frames=True
 aa.use_camera_cut_for_warm_up=False
 cvars=cfg.find_or_add_setting_by_class(unreal.MoviePipelineConsoleVariableSetting)
 for name,value in {'r.ScreenPercentage':100.0,'r.MotionBlurQuality':0.0,'t.MaxFPS':0.0}.items():
  assert cvars.add_or_update_console_variable(name,value)
 # Keep the scene's existing rendering quality and exposure, not a cinematic override.
unreal.log('REEL_RENDER_START mode='+mode+' jobs='+str(len(reel_queue.get_jobs()))+' output='+output)
def finished(executor,success):
 unreal.log('REEL_RENDER_FINISHED success='+str(success))
 os.makedirs(output,exist_ok=True)
 with open(os.path.join(output,'render_status.json'),'w') as f:json.dump({'success':success,'mode':mode,'jobs':[j.job_name for j in reel_queue.get_jobs()]},f,indent=2)
def error(executor,pipeline,fatal,message):unreal.log_error('REEL_RENDER_ERROR fatal='+str(fatal)+' '+message)
reel_executor=unreal.MoviePipelinePIEExecutor(subsystem)
reel_executor.on_executor_finished_delegate.add_callable_unique(finished)
reel_executor.on_executor_errored_delegate.add_callable_unique(error)
# The executor accepts an explicit queue, so do not replace the editor's UI queue.
reel_executor.execute(reel_queue)
