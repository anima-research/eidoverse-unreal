"""Frames resize from any edge (Eidoverse.TestHUDResize). Needs a windowed editor: synthetic Slate pointer events do not route under -RenderOffscreen."""
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'done': False}
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if st['t'] > 4 and bs and bs[0].get_editor_property('hud') and not st.get('shown'):
        st['shown'] = True; bs[0].get_editor_property('hud').show_frame('chat', True)
        unreal.log_warning('FRAME_VIS chat open=%s locked=?' % bs[0].get_editor_property('hud').is_frame_open('chat'))
    if st['t'] > 7 and not st['done']:
        st['done'] = True
        unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.TestHUDResize')
    if st['t'] > 10:
        unreal.unregister_slate_post_tick_callback(handle); level.editor_request_end_play(); unreal.SystemLibrary.quit_editor()
handle = unreal.register_slate_post_tick_callback(tick)
