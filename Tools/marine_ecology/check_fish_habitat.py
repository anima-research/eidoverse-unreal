"""Runtime containment check and close-up camera for independently moving fish."""
import unreal,sys,os,math
sys.path.insert(0,os.path.dirname(__file__))
from ecology_layout import height
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
eco=unreal.GameplayStatics.get_actor_of_class(w,unreal.MarineEcosystem)
groups=eco.get_components_by_class(unreal.InstancedStaticMeshComponent)
dry=[]
for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor):
 for c in a.get_components_by_class(unreal.BoxComponent):
  if 'AirVolume' in c.get_name() or c.component_has_tag('WaterAirVolume'):dry.append(c)
lowest=1e6;highest=-1e6;inside=0;below=0
for c in groups:
 for i in range(c.get_instance_count()):
  p=c.get_instance_transform(i,world_space=True).translation
  clearance=p.z-height(p.x,p.y);lowest=min(lowest,clearance);highest=max(highest,p.z)
  below+=clearance<35
  for box in dry:
   q=unreal.MathLibrary.inverse_transform_location(box.get_world_transform(),p);e=box.get_unscaled_box_extent()
   if abs(q.x)<e.x and abs(q.y)<e.y and abs(q.z)<e.z:inside+=1
unreal.log('FISH_CONTAINMENT count='+str(eco.fish_count)+' minimum_seabed_clearance='+str(lowest)+' highest_z='+str(highest)+' inside_dry_rooms='+str(inside)+' below_clearance='+str(below))
assert below==0 and inside==0 and highest<-100
p=groups[1].get_instance_transform(0,world_space=True).translation
diver=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Pawn) if a.actor_has_tag('OceanDiver'))
camera=p+unreal.Vector(-450,-250,80)
diver.set_actor_location(camera,False,True)
unreal.GameplayStatics.get_player_controller(w,0).set_control_rotation(unreal.MathLibrary.find_look_at_rotation(camera,p))
unreal.log('FISH_CONTAINMENT_PASS_AND_PREVIEW_READY')
