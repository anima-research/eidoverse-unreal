"""TTS, live: /tts on with tts_bot.ts as the endpoint, mic off; my say is chunked,
synthesized, played in order on the voice leg (voice_bot.ts measures what it hears from
me) and moves my mouth. TTS_RESULT <json>. Leaves TTS off afterwards."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8994/ws', os.environ['TTS_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'lv': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    w = editor.get_game_world()
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge) if w else []
    if bs: bs[0].set_tts_enabled(False)
    unreal.log_warning('TTS_RESULT ' + json.dumps(result, default=str))
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
        b.handle_interaction_command('/tts ws://127.0.0.1:8927')
        b.join_from_door(SERVER, WORLD, 'unreal-tts-test'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and since > 8:
        b.set_tts_enabled(True); return step('connect')
    if st['step'] == 'connect' and since > 4:
        result['diag'] = b.voice_diagnostic()
        b.say('Hello there! This is my own voice, speaking through the native client, and it keeps talking for a while so the listener hears it.')
        return step('speak')
    if st['step'] == 'speak':
        st['lv'] = max(st['lv'], b.get_voice_level('unreal-tts-test'))
        if since > 8:
            result['chunks_spoken'] = b.get_tts_spoken_chunks(); result['diag_after'] = b.voice_diagnostic()
            return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
