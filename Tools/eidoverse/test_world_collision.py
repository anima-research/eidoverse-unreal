import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b and b.endpoint=='ws://127.0.0.1:19002/ws'
p=unreal.GameplayStatics.get_player_pawn(w,0)
def trace(a,z):
    return unreal.SystemLibrary.line_trace_single(w,unreal.Vector(*a),unreal.Vector(*z),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[p],unreal.DrawDebugTrace.NONE)
assert trace((625,-200,150),(775,-200,150)), 'solid front wall'
assert not trace((625,-50,150),(775,-50,150)), 'door opening must not collide'
assert trace((100,-200,150),(550,-200,150)), 'imported GLB must have collision'
assert trace((0,0,500),(0,0,-50)), 'terrain must have collision'
unreal.log('WORLD_COLLISION_PASS: structural walls block, doorway open, imported GLB blocks, terrain blocks')
