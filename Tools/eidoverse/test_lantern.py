"""The lantern, live: 'sit' lists the say row then sit (best, Tab); Enter says it into chat; Tab
sits; 'mute' finds the mic by keyword; '/w' lists the whisper command; a typo + Tab does nothing.
LANTERN_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['LN_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('LANTERN_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 80: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; hud = b.get_editor_property('hud'); pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-lantern'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and isinstance(pawn, unreal.EidoverseExplorer) and since > 6:
        hud.open_lantern('sit'); result['sit_rows'] = list(hud.get_lantern_rows())[:4]
        st['lines'] = hud.get_visible_chat_line_count()
        hud.lantern_key('Enter'); result['open_after_enter'] = hud.is_lantern_open(); return step('said')
    if st['step'] == 'said' and since > 2:
        result['chat_grew'] = hud.get_visible_chat_line_count() > st['lines']
        hud.lantern_type('sit'); hud.lantern_key('Tab'); return step('sat')
    if st['step'] == 'sat' and since > 2:
        result['seated_after_tab'] = pawn.get_editor_property('seated') or pawn.get_editor_property('posture') == 'sit'
        hud.lantern_type('mute'); result['mute_rows'] = list(hud.get_lantern_rows())[:3]
        hud.lantern_type('/w'); result['cmd_rows'] = list(hud.get_lantern_rows())[:2]
        hud.lantern_type('qzxv'); hud.lantern_key('Tab'); result['typo_tab_still_open'] = hud.is_lantern_open()
        hud.lantern_key('Escape'); result['closed'] = not hud.is_lantern_open()
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
