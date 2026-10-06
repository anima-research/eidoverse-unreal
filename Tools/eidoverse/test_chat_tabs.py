"""Whisper tabs and autocomplete, live, with talk_bot.ts ('talker' whispers me on arrival):
the whisper opens @talker with 1 unread; opening it clears that and shows only the
conversation; Enter there whispers talker; drafts are per tab; "/wh" and "@ta" complete.
CHAT_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['CHAT_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('CHAT_RESULT ' + json.dumps(result, default=str))
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
    b = bs[0]; hud = b.get_editor_property('hud')
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-chat-test'); return step('wait')
    if st['step'] == 'wait':
        if 'talker' in hud.get_dm_tabs() or since > 40:
            result['tabs'] = list(hud.get_dm_tabs()); result['unread'] = hud.get_dm_unread('talker')
            result['all_lines'] = hud.get_visible_chat_line_count()
            hud.set_chat_draft('a public draft')
            hud.open_dm('talker')
            return step('dm')
    if st['step'] == 'dm' and since > 1:
        result['active'] = hud.get_active_dm(); result['unread_after_open'] = hud.get_dm_unread('talker')
        result['dm_lines'] = hud.get_visible_chat_line_count(); result['dm_draft'] = hud.chat_draft()
        hud.set_chat_draft('hello back, privately'); hud.submit_chat()
        return step('sent')
    if st['step'] == 'sent' and since > 2:
        result['dm_lines_after_send'] = hud.get_visible_chat_line_count()
        hud.close_dm('talker')
        result['draft_back_in_all'] = hud.chat_draft(); result['tabs_after_close'] = list(hud.get_dm_tabs())
        hud.set_chat_draft('/wh')
        result['complete_cmd'] = list(hud.get_autocomplete())
        hud.chat_key('Tab'); result['after_tab'] = hud.chat_draft()
        hud.set_chat_draft('hi @ta')
        result['complete_at'] = list(hud.get_autocomplete())
        hud.chat_key('Enter'); result['after_enter'] = hud.chat_draft()
        hud.set_chat_draft('/w talker')
        hud.submit_chat()
        return step('w')
    if st['step'] == 'w' and since > 1:
        result['w_opens_tab'] = hud.get_active_dm(); hud.set_chat_draft('')
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
