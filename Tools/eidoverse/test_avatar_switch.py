"""Switching avatars must not leave the old body's rigid parts behind.

Join wearing claude.vrm (rigid eyes, brows, mouth), then change to
mythos_painthair.vrm (fully skinned). Before the fix, the re-join destroyed the
avatar mesh but not the parts attached to it, which then floated beside the new
body. Logs AVATAR_SWITCH_RESULT <json>."""
import json, time
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
SERVER, WORLD = 'ws://127.0.0.1:8993/ws', 'ui-switch-%d' % (time.time() % 100000)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not editor.get_game_world():
    unreal.EditorLoadingAndSavingUtils.load_map('/Eidoverse/Maps/L_EidoverseClient')
    level.editor_request_begin_play()
st = {'t': 0.0, 'step': 'join', 'at': 0.0}
result = {}
def done(ok, why=''):
    result.update(ok=ok, why=why)
    unreal.log_warning('AVATAR_SWITCH_RESULT ' + json.dumps(result, default=str))
    unreal.unregister_slate_post_tick_callback(handle)
    level.editor_request_end_play()
    unreal.SystemLibrary.quit_editor()
def parts(pawn):
    # Static meshes and lights on the pawn, other than the stand-in proxy.
    meshes = [c for c in pawn.get_components_by_class(unreal.StaticMeshComponent) if c.get_attach_parent() and c.get_attach_parent().get_class().get_name() != 'CapsuleComponent'
              and c.get_class().get_name() != 'CameraProxyMeshComponent']  # editor-only camera gizmo
    lights = pawn.get_components_by_class(unreal.PointLightComponent)
    return {'meshes': len(meshes), 'lights': len(lights), 'bodies': len(pawn.get_components_by_class(unreal.EidoverseAvatarMesh))}
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
        b.set_editor_property('avatar_path', 'eidoverse/assets/vrms/claude.vrm')
        b.join_from_door(SERVER, WORLD, 'unreal-switch-test')
        return step('claude')
    if st['step'] == 'claude' and b.get_editor_property('joined') and pawn and pawn.get_components_by_class(unreal.EidoverseAvatarMesh) and since > 5:
        result['as_claude'] = parts(pawn)
        result['changed'] = b.change_avatar('eidoverse/assets/vrms/mythos_painthair.vrm')
        return step('switched')
    if st['step'] == 'switched' and since > 12:
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        result['as_mythos'] = parts(pawn)
        c, m = result['as_claude'], result['as_mythos']
        ok = c['meshes'] >= 5 and m['meshes'] == 0 and m['bodies'] == 1
        done(ok, '' if ok else 'rigid parts survived the switch')
handle = unreal.register_slate_post_tick_callback(tick)
