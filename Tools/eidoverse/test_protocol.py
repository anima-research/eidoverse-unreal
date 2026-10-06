"""Protocol/chat parity, live against the preview server with talk_bot.ts running:
join window of chat (recentChat), history paging, whispers once each, command table
(/who, /me, unknown never posted, /grant refused with the server's own text),
restore-where-you-were on reconnect. PROTOCOL_RESULT <json>."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['PROTO_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}


def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('PROTOCOL_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()


def lines(b):
    return [(l.sender, l.text) for l in b.get_editor_property('chat_history')]


def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 220: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud')
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']

    def step(n):
        st['step'] = n; st['at'] = st['t']

    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-proto-test')
        return step('joined')
    if st['step'] == 'joined':
        if not (b.get_editor_property('joined') and since > 5): return
        L = lines(b)
        result['join_window'] = [t for s, t in L[:3]]
        result['join_count'] = len([1 for s, t in L if s == 'talker'])
        return step('history')
    if st['step'] == 'history':
        if since < 0.5: b.request_earlier_chat(); return
        if since < 9: return
        result['after_history'] = len([1 for s, t in lines(b) if s == 'talker'])
        return step('commands')
    if st['step'] == 'commands':
        for c in ('/who', '/me waves', '/nonsense here', '/grant talker builder', '/goto talker', '/pushable', '/role'):
            b.handle_interaction_command(c)
        return step('cmd_wait')
    if st['step'] == 'cmd_wait' and since > 3:
        L = lines(b)
        result['narration'] = [t for s, t in L if s == '*'][-10:]
        result['posted_nonsense'] = any('nonsense' in t and s != '*' for s, t in L)
        result['whispers'] = [(s, t) for s, t in L if 'private hello' in t]
        b.whisper('talker', 'hi back')
        return step('whisper_echo')
    if st['step'] == 'whisper_echo' and since > 2:
        result['echo_lines'] = [(s, t) for s, t in lines(b) if t == 'hi back']
        pawn.set_actor_location(unreal.Vector(b.get_editor_property('origin_centimeters').x + 500,
                                              b.get_editor_property('origin_centimeters').y + 300, 200), False, True)
        return step('moved')
    if st['step'] == 'moved' and since > 3:
        loc = pawn.get_actor_location(); st['before'] = (loc.x, loc.y)
        b.disconnect()
        o = b.get_editor_property('origin_centimeters')
        pawn.set_actor_location(unreal.Vector(o.x - 400, o.y - 400, 300), False, True)
        b.join_from_door(SERVER, WORLD, 'unreal-proto-test')
        return step('rejoined')
    if st['step'] == 'rejoined' and since > 8 and b.get_editor_property('joined'):
        p = unreal.GameplayStatics.get_player_pawn(w, 0).get_actor_location()
        result['restore_before'] = [round(v) for v in st['before']]
        result['restore_after'] = [round(p.x), round(p.y)]
        result['restored'] = abs(p.x - st['before'][0]) < 60 and abs(p.y - st['before'][1]) < 60
        return step('end')
    if st['step'] == 'end' and since > 1: done(True)


handle = unreal.register_slate_post_tick_callback(tick)
