"""Destructive ONLY to the temporary PIE world; stop PIE after this test.
Checks live registry transforms, dry membership, light bindings and stale-slot removal.
"""
import time
import traceback
import unreal

world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world and 'UEDPIE' in world.get_path_name(),'Start PIE first; never run against the editor world.'
actors=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor)
controller=next(a for a in actors if isinstance(a,unreal.WaterOpticsController))
hab=next(a for a in actors if a.get_class().get_name()=='BP_Habitat_C')
sub=next(a for a in actors if a.get_class().get_name()=='BP_Cyclops_C')
diver=next(a for a in actors if a.get_class().get_name()=='BP_DiverPawn_C')
collection=controller.get_editor_property('collection')
box=next(c for c in hab.get_components_by_class(unreal.BoxComponent) if c.get_name()=='AirVolume')

def vector(name): return unreal.MaterialLibrary.get_vector_parameter_value(world,collection,name)
def near(a,b): return abs(a-b)<.02
def check_transforms():
    p=box.get_world_location(); e=box.get_scaled_box_extent()
    slots=[i for i in range(16) if vector(f'OpticsAir{i}C').a>.5 and near(vector(f'OpticsAir{i}C').r,p.x)]
    assert len(slots)==1,slots
    i=slots[0]; c=vector(f'OpticsAir{i}C'); ext=vector(f'OpticsAir{i}E'); x=vector(f'OpticsAir{i}X')
    assert near(c.g,p.y) and near(c.b,p.z),(c,p)
    assert near(ext.r,e.x) and near(ext.g,e.y) and near(ext.b,e.z),(ext,e)
    forward=box.get_forward_vector()
    assert near(x.r,forward.x) and near(x.g,forward.y) and near(x.b,forward.z),(x,forward)
    unreal.log('OPTICS_RUNTIME_PASS transformed air-box position, scaled extents and axes')

def initial():
    assert controller.get_editor_property('air_volume_count')==2
    lights=[c for a in actors for c in a.get_components_by_class(unreal.LocalLightComponent)]
    assert len(lights)==13
    assert all(isinstance(c.get_editor_property('light_function_material'),unreal.MaterialInstanceDynamic) for c in lights)
    for a in actors:
        if isinstance(a,unreal.ExponentialHeightFog):
            assert not a.get_component_by_class(unreal.ExponentialHeightFogComponent).is_visible()
    unreal.log('OPTICS_RUNTIME_PASS 2 air volumes; 13 per-light materials; global fog disabled')
    hab.set_actor_scale3d(unreal.Vector(1.25,.8,1.1))
    hab.set_actor_rotation(unreal.Rotator(pitch=8,yaw=25,roll=12),True)

def enter():
    check_transforms()
    # Restore room orientation for the membership test, and place the camera inside it.
    hab.set_actor_scale3d(unreal.Vector(1,1,1))
    hab.set_actor_rotation(unreal.Rotator(),True)
    diver.set_actor_location(box.get_world_location(),False,True)

def overlap():
    assert diver.get_editor_property('InsideHabitat'), 'Camera should be in habitat'
    subbox=next(c for c in sub.get_components_by_class(unreal.BoxComponent) if c.get_name()=='AirVolume')
    delta=box.get_world_location()-subbox.get_world_location()
    sub.set_actor_location(sub.get_actor_location()+delta,False,True)
    unreal.log('OPTICS_RUNTIME_PASS camera-in-habitat membership')

def leave_one():
    assert diver.get_editor_property('InsideHabitat')
    hab.set_actor_location(hab.get_actor_location()+unreal.Vector(5000,0,0),False,True)

def destroy_room():
    assert diver.get_editor_property('InsideHabitat'),'Leaving one overlapping box must not clear another box membership'
    unreal.log('OPTICS_RUNTIME_PASS overlapping-room exit retains dry membership')
    hab.destroy_actor()

def final():
    assert controller.get_editor_property('air_volume_count')==1
    assert sum(vector(f'OpticsAir{i}C').a>.5 for i in range(16))==1
    unreal.log('OPTICS_RUNTIME_PASS deleted room leaves no ghost registry slots')
    unreal.log('OPTICS_RUNTIME_TESTS_SUCCESS; STOP PIE to discard test-world changes')

steps=[initial,enter,overlap,leave_one,destroy_room,final]
state={'next':time.monotonic()+3,'index':0}
def tick(dt):
    if time.monotonic()<state['next']: return
    try:
        steps[state['index']]()
        state['index']+=1
        state['next']=time.monotonic()+3
        if state['index']==len(steps): unreal.unregister_slate_post_tick_callback(handle)
    except Exception:
        unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error('OPTICS_RUNTIME_TESTS_FAILED '+traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
