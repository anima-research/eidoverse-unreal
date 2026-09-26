"""Runtime-only local Commons Lumen trial; does not save assets or world edits."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
pp=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PostProcessVolume) if a.get_editor_property('priority')==100)
s=pp.get_editor_property('settings')
s.set_editor_property('override_dynamic_global_illumination_method',True)
s.set_editor_property('dynamic_global_illumination_method',unreal.DynamicGlobalIlluminationMethod.LUMEN)
s.set_editor_property('override_reflection_method',True)
s.set_editor_property('reflection_method',unreal.ReflectionMethod.LUMEN)
s.set_editor_property('override_lumen_ray_lighting_mode',True)
s.set_editor_property('lumen_ray_lighting_mode',unreal.LumenRayLightingModeOverride.HIT_LIGHTING)
pp.set_editor_property('settings',s)
lights=0
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
    # Only server-authored lamps, not the hidden ocean prototype.
    if not a.get_editor_property('hidden') and a.get_editor_property('tags'):
        for c in a.get_components_by_class(unreal.PointLightComponent):
            c.set_cast_shadows(True)
            lights+=1
    for c in a.get_components_by_class(unreal.EidoverseAvatarMesh):
        c.set_cast_shadow(True)
unreal.log('COMMONS_LUMEN_TRIAL: hit lighting GI/reflections, avatar shadows, %d shadowed lamps'%lights)
