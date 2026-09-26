import unreal,gc,types
for obj in gc.get_objects():
 if isinstance(obj,types.FunctionType) and obj.__code__.co_filename.endswith('/spectral_ocean/profile_live.py'):
  h=obj.__globals__.get('profile_handle')
  if h:
   try:unreal.unregister_slate_post_tick_callback(h)
   except Exception:pass
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
