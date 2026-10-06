"""Reach, live: a bot reaches for my left shoulder (narrated once), I /touch the bot's
shoulder (my arm solves; reached attested only when the hand arrives). REACH_RESULT."""
import json, os
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', os.environ['REACH_WORLD']
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('REACH_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def tick(dt):
    st['t'] += dt
    w = editor.get_game_world()
    if st['t'] > 150: return done(False, 'timeout at ' + st['step'])
    if not w: return
    bs = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.EidoverseBridge)
    if not bs or not bs[0].get_editor_property('hud'): return
    b = bs[0]; pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    since = st['t'] - st['at']
    def step(n): st['step'] = n; st['at'] = st['t']
    if st['step'] == 'join' and st['t'] > 3:
        b.join_from_door(SERVER, WORLD, 'unreal-reach-test'); return step('wait')
    if st['step'] == 'wait':
        if not (b.get_editor_property('joined') and b.get_editor_property('loaded_avatar_count') >= 1 and since > 8): return
        o = b.get_editor_property('origin_centimeters')
        pawn.set_actor_location(unreal.Vector(o.x + 100, o.y, 100), False, True)   # 1 m in front of the bot (wire z=1)... close enough to touch
        return step('touch')
    if st['step'] == 'touch' and since > 2:
        b.handle_interaction_command('/touch reach-bot shoulder_l')
        return step('measure')
    if st['step'] == 'measure' and since > 3:
        me = unreal.GameplayStatics.get_player_pawn(w, 0).get_components_by_class(unreal.EidoverseAvatarMesh)[0]
        bot = b.get_peer_body('reach-bot')
        hand = me.human_bone_position('rightHand'); sh = bot.human_bone_position('leftUpperArm') if bot else None
        result['my_hand_to_target_cm'] = round((hand - sh).length()) if hand and sh else None
        result['narration'] = [l.text for l in b.get_editor_property('chat_history') if l.sender == '*'][-6:]
        return step('end')
    if st['step'] == 'end' and since > .5: done(True)
handle = unreal.register_slate_post_tick_callback(tick)
