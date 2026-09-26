"""Read-only verification of the installed default, not the runtime trial."""
import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
b=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseBridge)
assert b.joined and b.endpoint=='ws://127.0.0.1:19002/ws' and b.world_name=='commons'
assert b.use_lumen_lighting and 'hardware ray tracing' in b.world_lighting_status
pp=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PostProcessVolume) if a.get_editor_property('priority')==100)
s=pp.get_editor_property('settings')
assert s.get_editor_property('dynamic_global_illumination_method')==unreal.DynamicGlobalIlluminationMethod.LUMEN
assert s.get_editor_property('reflection_method')==unreal.ReflectionMethod.LUMEN
assert s.get_editor_property('lumen_ray_lighting_mode')==unreal.LumenRayLightingModeOverride.HIT_LIGHTING
lamps=[];avatars=[]
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
    if not a.get_editor_property('hidden') and a.get_editor_property('tags'):
        lamps.extend(a.get_components_by_class(unreal.PointLightComponent))
    avatars.extend(a.get_components_by_class(unreal.EidoverseAvatarMesh))
assert len(lamps)==9 and all(c.get_editor_property('cast_shadows') for c in lamps)
assert avatars and all(c.get_editor_property('cast_shadow') for c in avatars)
scene=unreal.GameplayStatics.get_actor_of_class(w,unreal.EidoverseScene)
assert scene.loaded_models==70 and scene.failed_models==0
unreal.log('COMMONS_LUMEN_PASS: hardware Lumen GI/reflections, hit lighting, nine shadowed lamps, avatar shadows, 70/70 models')
