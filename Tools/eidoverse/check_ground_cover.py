"""Read-only checks for the rebuilt Commons ground cover and avatar tag."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert s.loaded_models==70 and s.failed_models==0
cover=s.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
assert len(cover)==1
c=cover[0]
assert 1000<c.get_instance_count()<=12000
assert c.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
assert not c.get_editor_property('cast_shadow')
assert not c.get_editor_property('visible_in_ray_tracing')
assert c.get_editor_property('instance_end_cull_distance')==4500
assert c.get_editor_property('num_custom_data_floats')==5
assert c.static_mesh.get_num_lods()==3
assert c.get_material(0).get_name()=='M_EidoverseGroundCover'
m=next(c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
       for c in a.get_components_by_class(unreal.EidoverseAvatarMesh) if c.get_num_bones()==445)
label=m.get_owner().get_components_by_class(unreal.TextRenderComponent)[0]
head=m.human_bone_position('head')
assert head is not None
delta=label.get_world_location()-head
assert abs(delta.x)<1 and abs(delta.y)<1 and 39<delta.z<41, str(delta)
assert label.get_editor_property('vertical_alignment')==unreal.VerticalTextAligment.EVRTA_TEXT_BOTTOM
unreal.log('GROUND_COVER_PASS: %d instanced tufts, three LODs, 45m cull, no collision/RT/shadows; tag 40cm above posed head'%c.get_instance_count())
