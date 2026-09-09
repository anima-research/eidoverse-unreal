"""Repair wrapped pitch and add low-speed trim in the existing submarine graph."""
import unreal
from editor_toolset.toolsets.blueprint import BlueprintTools as B
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
bp=unreal.load_asset('/Game/Underwater/Blueprints/BP_Cyclops')
g=B.get_graph(bp,'EventGraph')
def existing(name):
 n=unreal.find_object(None,g.get_path_name()+'.'+name);assert n,name
 return n
def pin(n,name,out=False):
 info=B.get_node_infos([n])[0]
 return next(p.pin_id for p in (info.output_pins if out else info.input_pins) if p.name==name)
def link(a,ap,b,bp):B.connect_pins(pin(a,ap,True),pin(b,bp))
def val(n,p,v):B.set_pin_value(pin(n,p),str(v))
def disconnect(n,p):
 info=B.get_node_infos([n])[0]
 pinfo=next(x for x in info.input_pins if x.name==p)
 for source in pinfo.connected_pins:B.break_pins(source,pinfo.pin_id)
created=[]
def node(t):
 n=B.create_node(g,t,unreal.IntPoint(13000+(len(created)%5)*320,1000+(len(created)//5)*320))
 created.append(n);return n
assert not B.find_nodes(g,'Normalize Axis'),'Already installed or partial installation; inspect before rerunning'
tick=existing('K2Node_Event_7');valid=existing('K2Node_MacroInstance_1')
controller=existing('K2Node_CallFunction_60');break_control=existing('K2Node_CallFunction_62')
make_target=existing('K2Node_CallFunction_68');current=existing('K2Node_CallFunction_69')
interp=existing('K2Node_CallFunction_70');apply=existing('K2Node_CallFunction_71')
# Build and validate new data paths before touching the existing execution chain.
norm=node('Math|Rotator|NormalizeAxis');link(break_control,'Pitch',norm,'Angle')
clamp=node('Math|Float|Clamp(Float)');link(norm,'ReturnValue',clamp,'Value');val(clamp,'Min',-25);val(clamp,'Max',25)
velocity=node('Transformation|GetVelocity');speed=node('Math|Vector|VectorLength');link(velocity,'ReturnValue',speed,'A')
idle=node('Math|Float|MapRangeClamped');link(speed,'ReturnValue',idle,'Value')
for p,v in [('InRangeA',15),('InRangeB',100),('OutRangeA',1),('OutRangeB',0)]:val(idle,p,v)
trim=node('Math|Interpolation|FInterpTo');link(clamp,'ReturnValue',trim,'Current');val(trim,'Target',0);val(trim,'InterpSpeed',0.6);link(tick,'DeltaSeconds',trim,'DeltaTime')
blend=node('Math|Float|Lerp');link(clamp,'ReturnValue',blend,'A');link(trim,'ReturnValue',blend,'B');link(idle,'ReturnValue',blend,'Alpha')
control_target=node('Math|Rotator|MakeRotator');link(blend,'ReturnValue',control_target,'Pitch');link(break_control,'Yaw',control_target,'Yaw')
set_control=node('Pawn|SetControlRotation');link(controller,'ReturnValue',set_control,'self');link(control_target,'ReturnValue',set_control,'NewRotation')
# Unoccupied submarine also levels its pitch/roll, preserving heading and position.
break_actor=node('Math|Rotator|BreakRotator');link(current,'ReturnValue',break_actor,'InRot')
level=node('Math|Rotator|MakeRotator');link(break_actor,'Yaw',level,'Yaw')
free_interp=node('Math|Interpolation|RInterpTo');link(current,'ReturnValue',free_interp,'Current');link(level,'ReturnValue',free_interp,'Target');link(tick,'DeltaSeconds',free_interp,'DeltaTime');val(free_interp,'InterpSpeed',0.6)
free_apply=node('Transformation|SetActorRotation');link(free_interp,'ReturnValue',free_apply,'NewRotation')
# Replace raw 0..360 pitch comparisons with signed, bounded pitch. Keep old nodes
# disconnected as a readable historical reference, not as a second controller.
disconnect(make_target,'Pitch');link(clamp,'ReturnValue',make_target,'Pitch')
disconnect(apply,'execute');link(valid,'Is Valid',set_control,'execute');link(set_control,'then',apply,'execute')
link(valid,'Is Not Valid',free_apply,'execute')
val(interp,'InterpSpeed',2.0)
B.compile_blueprint(bp,warnings_as_errors=True)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('VESSEL_CONTROL_REFINEMENT_SUCCESS')
