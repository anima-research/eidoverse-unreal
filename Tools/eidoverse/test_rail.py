"""Rail pins and edge, live (no server needed): all windows pinned by default; unpinning a
closed window takes its icon off the rail, opening it brings it back; the rail moves to the
right edge and back. Restores the person's pins and edge. RAIL_RESULT."""
import json
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'start', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('RAIL_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 40: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    hud = bs[0].get_editor_property('hud')
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'start' and st['t'] > 4:
        hud.show_frame('settings', False)
        result['visible_default'] = hud.get_rail_visible_count()
        hud.set_rail_pinned('settings', False); return step('unpinned')
    if st['step'] == 'unpinned' and since > .5:
        result['after_unpin'] = hud.get_rail_visible_count()
        hud.show_frame('settings', True); return step('opened')
    if st['step'] == 'opened' and since > .5:
        result['while_open'] = hud.get_rail_visible_count()
        hud.show_frame('settings', False); hud.set_rail_pinned('settings', True)
        hud.set_rail_right(True); return step('right')
    if st['step'] == 'right' and since > .5:
        hud.set_rail_right(False); result['restored'] = hud.get_rail_visible_count(); return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
