"""Voice in the world, live, against voice_bot.ts on an SFU server (VOICE_TRANSPORT=sfu):
consent on (/hear on), a test tone published instead of the mic, the bot's tone must
arrive as a speaker, play from its avatar at a distance volume, and move its mouth.
VOICE_RESULT <json>. Leaves Eidoverse.Audio.HearVoices off afterwards (privacy default)."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = os.environ.get('VOICE_URL', 'ws://127.0.0.1:8994/ws'), os.environ['VOICE_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0, 'levels': []}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    w = editor.get_game_world()
    if w: unreal.SystemLibrary.execute_console_command(w, 'Eidoverse.Audio.HearVoices 0')
    unreal.log_warning('VOICE_RESULT ' + json.dumps(result, default=str))
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
        b.set_voice_test_tone(660, .5)
        b.handle_interaction_command('/hear on')
        b.join_from_door(SERVER, WORLD, 'unreal-voice-test'); return step('wait')
    if st['step'] == 'wait':
        if 'voicebot' in b.get_voice_speakers() or since > 40:
            result['speakers'] = list(b.get_voice_speakers()); return step('listen')
    if st['step'] == 'listen':
        st['levels'].append(b.get_voice_level('voicebot'))
        if since < 6: return
        a = b.get_peer_actor('voicebot'); m = b.get_peer_body('voicebot')
        auds = a.get_components_by_class(unreal.AudioComponent) if a else []
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        result['peer_distance_m'] = round(a.get_actor_location().distance(pawn.get_actor_location()) / 100, 2) if a else None
        result['audio_on_avatar'] = len(auds)
        result['audio_volume'] = round(auds[0].get_editor_property('volume_multiplier'), 3) if auds else None
        result['audio_playing'] = auds[0].is_playing() if auds else None
        result['level_max'] = round(max(st['levels']), 3); result['level_mean'] = round(sum(st['levels']) / len(st['levels']), 3)
        result['diag'] = b.voice_diagnostic()
        # Walk 25 m away: the rolloff (3 -> 20 m) must take it to silence.
        o = b.get_editor_property('origin_centimeters')
        st['home'] = pawn.get_actor_location(); h = st['home']
        pawn.set_actor_location(unreal.Vector(h.x - 2500, h.y, h.z + 20), False, True)
        return step('far')
    if st['step'] == 'far' and since > 3:
        a = b.get_peer_actor('voicebot'); auds = a.get_components_by_class(unreal.AudioComponent) if a else []
        result['far_volume'] = round(auds[0].get_editor_property('volume_multiplier'), 4) if auds else None
        o = b.get_editor_property('origin_centimeters'); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        h = st['home']; pawn.set_actor_location(unreal.Vector(h.x, h.y, h.z + 20), False, True)
        return step('near')
    if st['step'] == 'near' and since > 3:
        a = b.get_peer_actor('voicebot'); auds = a.get_components_by_class(unreal.AudioComponent) if a else []
        result['near_again_volume'] = round(auds[0].get_editor_property('volume_multiplier'), 3) if auds else None
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        result['near_again_m'] = round(a.get_actor_location().distance(pawn.get_actor_location()) / 100, 2) if a else None
        result['near_again_level'] = round(b.get_voice_level('voicebot'), 3); result['diag2'] = b.voice_diagnostic()
        b.handle_interaction_command('/hear off')
        return step('off')
    if st['step'] == 'off' and since > 4:
        a = b.get_peer_actor('voicebot'); auds = a.get_components_by_class(unreal.AudioComponent) if a else []
        result['volume_after_off'] = round(auds[0].get_editor_property('volume_multiplier'), 4) if auds else None
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
