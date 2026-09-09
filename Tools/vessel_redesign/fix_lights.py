"""Repair torch defaults and place shadow-casting floodlights ahead of bow pods."""
import unreal
from editor_toolset.toolsets.actor import ActorTools as A
from editor_toolset.toolsets.blueprint import BlueprintTools as B
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def components(o):return {c.get_name().removesuffix('_GEN_VARIABLE'):c for c in A.get_components(o)}
def configure(o,diver):
 if diver and o.get_name().startswith('Default__'):o.set_editor_property('TorchIntensity',800.)
 cs=components(o)
 if diver and cs.get('MeshComponent0'):cs['MeshComponent0'].set_editor_property('cast_shadow',False)
 for name,loc in ([('Torch',(55,22,-12))] if diver else [('Headlight_0',(232,-205,-135)),('Headlight_1',(232,205,-135))]):
  c=components(o)[name]
  for k,v in [('relative_location',unreal.Vector(*loc)),('intensity_units',unreal.LightUnits.CANDELAS),('intensity',800. if diver else 6000.),('attenuation_radius',3000. if diver else 4500.),('cast_shadows',True),('visible',True)]:c.set_editor_property(k,v)
  c=components(o)[name]
  c.set_relative_location(unreal.Vector(*loc),False,True)
  c.set_intensity(800. if diver else 6000.)
  c.set_visibility(True)
for name in ('BP_DiverPawn','BP_Cyclops'):
 bp=unreal.load_asset('/Game/Underwater/Blueprints/'+name)
 configure(B.get_default_object(bp),name=='BP_DiverPawn')
 B.compile_blueprint(bp);unreal.EditorAssetLibrary.save_loaded_asset(bp)
 for a in EAS.get_all_level_actors():
  if a.get_class().get_name()==name+'_C':configure(a,name=='BP_DiverPawn')
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('LIGHT_REPAIR_SUCCESS')
