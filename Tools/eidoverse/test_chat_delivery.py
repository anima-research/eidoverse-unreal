"""PIE regression: fixture only. Chat receipt, timeout, private send, ocean seams."""
import math
import time
import urllib.request
import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
started = time.monotonic()
phase = 0
bridge = None
probe_at = 0

def probe(world, private=False):
    global probe_at
    probe_at = time.monotonic()
    unreal.SystemLibrary.execute_console_command(world, 'Eidoverse.TestChatUI' + (' private' if private else ''))

def echo(enabled):
    req = urllib.request.Request('http://127.0.0.1:18995/echo/' + ('on' if enabled else 'off'), method='POST')
    with urllib.request.urlopen(req, timeout=2) as response:
        assert response.status == 200

def tick(dt):
    global phase, bridge
    try:
        if time.monotonic() - started > 50:
            raise RuntimeError('delivery regression timed out at phase ' + str(phase))
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            return
        if bridge is None:
            bridge = unreal.GameplayStatics.get_actor_of_class(world, unreal.EidoverseBridge)
            bridge.endpoint = 'ws://127.0.0.1:18995/ws'
            bridge.world_name = 'water-local-test'
            bridge.connect()
        if phase == 0 and bridge.joined and len(bridge.chat_history) == 2:
            ocean = unreal.GameplayStatics.get_actor_of_class(world, unreal.SpectralOcean)
            for x in [-12800, -1e-6, 0, 400, 12800]:
                for y in [-12800, -1e-6, -1e-20, 0, 12800]:
                    assert math.isfinite(ocean.get_surface_height(unreal.Vector(x,y,0)))
            assert ocean.update_count > 0
            phase = 1
            probe(world)
        elif phase == 1 and bridge.chat_send_status.startswith('Delivered') and time.monotonic()-probe_at > .6:
            assert len(bridge.chat_history) == 3
            unreal.log('CHAT_DELIVERY_CONFIRMED_PASS')
            echo(False)
            phase = 2
            probe(world)
        elif phase == 2 and bridge.chat_send_status.startswith('Delivery unconfirmed'):
            assert 'Message restored' in bridge.chat_send_status
            assert len(bridge.chat_history) == 3
            unreal.log('CHAT_DELIVERY_TIMEOUT_PASS')
            echo(True)
            phase = 3
            probe(world, True)
        elif phase == 3 and len(bridge.chat_history) == 5:
            assert 'Private message sent' in bridge.chat_send_status
            assert sum(line.private for line in bridge.chat_history) == 3
            bridge.close_chat()
            pc = unreal.GameplayStatics.get_player_controller(world,0)
            assert not pc.is_move_input_ignored() and not pc.is_look_input_ignored()
            unreal.unregister_slate_post_tick_callback(handle)
            unreal.log('CHAT_DELIVERY_OCEAN_PASS: receipt, timeout/draft recovery, private chat, input restoration, seam sampling')
    except Exception:
        unreal.unregister_slate_post_tick_callback(handle)
        raise

handle = unreal.register_slate_post_tick_callback(tick)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
