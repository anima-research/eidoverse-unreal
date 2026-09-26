"""Configure the two temporary PIE components added with PrimitiveTools.
No source avatar, saved level, lights, or server state are changed.
The block and backboard disappear when Play stops.
"""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
m=next(c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
       for c in a.get_components_by_class(unreal.EidoverseAvatarMesh) if c.get_num_bones()==445)
parts={c.get_name():c for c in m.get_owner().get_components_by_class(unreal.StaticMeshComponent)}
block=parts['ChestLightShadowBlock'];receiver=parts['ChestLightShadowReceiver']
lamp=m.emissive_lights[0]
origin=lamp.get_world_location()
front=m.get_owner().get_actor_forward_vector()
for component,distance,height in [(block,42,-5),(receiver,100,-5)]:
    component.set_mobility(unreal.ComponentMobility.MOVABLE)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(True)
    component.set_visibility(True)
    component.set_hidden_in_game(False)
    component.set_world_location(origin+front*distance+unreal.Vector(0,0,height),False,True)
    material=component.create_dynamic_material_instance(0,unreal.load_asset('/Game/Underwater/Avatars/Materials/M_EidoverseSurface'))
    material.set_vector_parameter_value('baseColorFactor',unreal.LinearColor(.55,.55,.55,1))
    material.set_scalar_parameter_value('metallicFactor',0)
    material.set_scalar_parameter_value('roughnessFactor',.95)
assert lamp.get_editor_property('cast_shadows')
unreal.log('CHEST_SHADOW_TEST_READY: 8x12x26cm opaque block 42cm in front of lamp; matte 90cm backboard at 1m; original pulsing lamp unchanged')
