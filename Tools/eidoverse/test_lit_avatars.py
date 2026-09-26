"""Inspect loaded remote meshes and their rigid parts in the local PIE fixture."""
import json
from pathlib import Path
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, "Run test_avatars.py and wait for its success marker first"
avatars = []
for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
    meshes = actor.get_components_by_class(unreal.EidoverseAvatarMesh)
    if not meshes:
        continue
    materials = []
    for component in actor.get_components_by_class(unreal.MeshComponent):
        for index in range(component.get_num_materials()):
            material = component.get_material(index)
            assert material, "Missing runtime material"
            base = material.get_base_material()
            shading = base.get_editor_property("shading_model")
            assert shading == unreal.MaterialShadingModel.MSM_DEFAULT_LIT, str(shading)
            materials.append({"component": component.get_name(), "slot": index,
                              "base": base.get_path_name(), "shading": str(shading)})
    assert materials
    avatars.append({"actor": actor.get_name(), "bones": meshes[0].get_num_bones(),
                    "materials": materials})
assert len(avatars) >= 2
Path('/Users/olena/unrealtest/eidoverse/lit_avatar_report.json').write_text(
    json.dumps(avatars, indent=2))
unreal.log("EIDOVERSE_LIT_AVATARS_PASS " + json.dumps(
    [{"bones": a["bones"], "material_slots": len(a["materials"])} for a in avatars]))
