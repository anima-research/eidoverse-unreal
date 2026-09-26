"""Read-only live check after joining local Commons with the winged preview."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
meshes=[c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
        for c in a.get_components_by_class(unreal.EidoverseAvatarMesh) if c.get_num_bones()==445]
assert len(meshes)==1
m=meshes[0]
lights=m.get_editor_property('emissive_lights')
assert len(lights)==1,'Only lampglass, not dim gold trim, should emit a light'
assert any('lampglass' in str(n).lower() for n in m.get_material_slot_names()),'Keep authored material names for pulse behavior'
light=lights[0]
assert light.get_attach_parent()==m and str(light.get_attach_socket_name())!='None'
assert light.get_editor_property('cast_shadows')
assert light.get_editor_property('attenuation_radius')==1200
p=light.get_world_location()-m.get_world_location()
assert 120<p.z<165,'Chest lamp must not be at the origin or head'
assert light.get_editor_property('intensity')<=40
unreal.log('MYTHOS_EMISSION_PASS: one bone-attached chest lamp, shadow casting, bounded radius/intensity; '+str(light.get_attach_socket_name()))
