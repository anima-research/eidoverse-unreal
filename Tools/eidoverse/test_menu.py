"""The ∃ waterfall, live: top rows are Save/Load (dead, saying why), Panels ▸, Log in (dead on a
door-key server), Help (H), Keys, About ▸. A press on Save keeps the menu open and toasts; the
About flyout reads the server's /version; Keys opens help and leaves the menu. A typed name with
< > " is refused at the door. A shot shows the Panels flyout. MENU_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['MENU_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('MENU_RESULT ' + json.dumps(result, default=str))
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
        result['refused_typed'] = not b.join_from_door(SERVER, WORLD, 'a<b>')
        result['refusal'] = b.get_editor_property('connection_status')
        b.join_from_door(SERVER, WORLD, 'unreal-menu'); return step('wait')
    if st['step'] == 'wait' and b.get_editor_property('joined') and isinstance(pawn, unreal.EidoverseExplorer) and since > 5:
        result['world_panel_open'] = st['world_was'] = hud.is_frame_open('world')
        hud.set_pointer_mode(True); hud.set_editor_property('menu_auto_close', False)
        hud.set_menu_open(True); return step('open')
    if st['step'] == 'open' and since > 1:
        result['rows'] = list(hud.get_menu_rows())
        hud.press_menu_row('save'); result['open_after_save'] = hud.is_menu_open()
        hud.open_menu_sub('about'); return step('about')
    if st['step'] == 'about' and since > 2.5:
        result['about'] = list(hud.get_about_lines()); result['sub'] = str(hud.get_menu_sub())
        hud.open_menu_sub('panels'); return step('panels')
    if st['step'] == 'panels' and since > 1:
        result['sub2'] = str(hud.get_menu_sub())
        unreal.SystemLibrary.execute_console_command(w, 'shot showui'); return step('keys')
    if st['step'] == 'keys' and since > 2:
        result['menu_still_open'] = hud.is_menu_open()
        hud.press_menu_row('keys'); result['open_after_keys'] = hud.is_menu_open()
        hud.show_frame('world', True); hud.world_action('sky:clock:Asia/Tokyo'); return step('real')
    if st['step'] == 'real' and since > 1.5:
        result['sky_time_hidden_real'] = hud.is_sky_time_hidden()
        hud.world_action('sky:clock:authored'); return step('authored')
    if st['step'] == 'authored' and since > 1.5:
        result['sky_time_hidden_authored'] = hud.is_sky_time_hidden()
        hud.show_frame('world', st['world_was'])   # leave the saved layout as it was
        return done(True)
handle = unreal.register_slate_post_tick_callback(tick)
