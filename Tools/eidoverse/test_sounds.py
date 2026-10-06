"""Placed sounds, live: a wav and an m4a on two crates must decode and play. SOUNDS_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['SND_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('SOUNDS_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 120: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-sound-test'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 12:
        out = {}
        for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
            if a.tags and str(a.tags[0]) in ('wavbox', 'm4abox'):
                cs = a.get_components_by_class(unreal.AudioComponent)
                out[str(a.tags[0])] = [c.is_playing() for c in cs]
        result['playing'] = out
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
