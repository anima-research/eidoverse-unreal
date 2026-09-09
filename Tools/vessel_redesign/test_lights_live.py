"""Runtime outputs and cone-clearance regression; temporary PIE camera setup."""
import unreal,math
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world();assert w
actors=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Actor)
diver=next(a for a in actors if a.get_class().get_name()=='BP_DiverPawn_C')
sub=next(a for a in actors if a.get_class().get_name()=='BP_Cyclops_C')
torch=next(c for c in diver.get_components_by_class(unreal.SpotLightComponent) if c.get_name()=='Torch')
assert diver.get_editor_property('TorchIntensity')==800,diver.get_editor_property('TorchIntensity')
assert torch.get_editor_property('intensity')==800,torch.get_editor_property('intensity')
assert torch.get_editor_property('visible')
unreal.log('TORCH_RUNTIME_PASS intensity=800cd; visible; correct Tick-driven value')
for light in sub.get_components_by_class(unreal.SpotLightComponent):
 if not light.get_name().startswith('Headlight'):continue
 assert light.get_editor_property('intensity')==6000
 start=light.get_world_location();rot=light.get_world_rotation()
 forward=unreal.MathLibrary.get_forward_vector(rot);right=unreal.MathLibrary.get_right_vector(rot);up=unreal.MathLibrary.get_up_vector(rot)
 blocked=[]
 for cone in (0,19,37):
  for j in range(16 if cone else 1):
   theta=j*math.tau/16
   direction=forward+(right*math.cos(theta)+up*math.sin(theta))*math.tan(math.radians(cone))
   hit=unreal.SystemLibrary.line_trace_single(w,start,start+direction*1200,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[a for a in actors if a!=sub],unreal.DrawDebugTrace.NONE)
   if hit and hit.to_tuple()[0]:blocked.append((cone,j,hit.to_tuple()[10].get_name()))
 assert not blocked,(light.get_name(),blocked)
 unreal.log('FLOODLIGHT_CLEARANCE_PASS '+light.get_name()+' 33 cone rays clear of hull/fittings; 6000cd')
snow=next(a for a in actors if isinstance(a,unreal.NiagaraActor) and 'Marine Snow' in a.get_actor_label())
assert snow.get_component_by_class(unreal.NiagaraComponent).is_active()
unreal.log('MARINE_SNOW_RUNTIME_ACTIVE')
pc=unreal.GameplayStatics.get_player_controller(w,0);pc.possess(diver)
diver.set_actor_location(unreal.Vector(6700,-3300,-1850),False,True)
pc.set_control_rotation(unreal.Rotator(pitch=-5,yaw=180,roll=0))
diver.get_movement_component().stop_movement_immediately()
