"""Read-only check after joining Local Commons with mythos-preview.ts running."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
assert b.loaded_avatar_count>=1
meshes=[c for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
        for c in a.get_components_by_class(unreal.EidoverseAvatarMesh)
        if c.get_num_bones()==445]
assert len(meshes)==1
m=meshes[0]
assert m.get_bone_index('L_Wing_Upper')>=0 and m.get_bone_index('R_Wing_Upper')>=0
assert m.get_num_materials()>=9
assert all(m.get_material(i) for i in range(m.get_num_materials()))
s=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert s.loaded_models==70 and s.failed_models==0
unreal.log('MYTHOS_PREVIEW_PASS: 445 bones, both wing chains, %d materials, 70/70 world models'%m.get_num_materials())
