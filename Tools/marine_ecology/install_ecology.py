"""Install a depth-zoned, navigable temperate shelf. Safe deterministic re-run."""
import unreal,math,random,os,sys,json
sys.path.insert(0,os.path.dirname(__file__))
import ecology_layout,importlib
importlib.reload(ecology_layout)
from ecology_layout import height,reserved,REEFS
from editor_toolset.toolsets.actor import ActorTools as A
ROOT='/Game/Underwater/Ecology';EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();R=random.Random(9126)
def prop(o,n,v):o.set_editor_property(n,v)
def vec(v):return unreal.Vector(*v)
def actor(label,cls=unreal.Actor):
 existing=next((a for a in EAS.get_all_level_actors() if a.get_actor_label()==label and 'MarineEcology' in [str(t) for t in a.tags]),None)
 a=existing or EAS.spawn_actor_from_class(cls,unreal.Vector())
 a.set_actor_label(label);a.set_folder_path('Marine Ecology');prop(a,'tags',['MarineEcology'])
 return a
def mesh_actor(label,name,location,rotation=None,moving=False):
 a=actor(label,unreal.StaticMeshActor);c=a.get_component_by_class(unreal.StaticMeshComponent)
 prop(c,'mobility',unreal.ComponentMobility.MOVABLE if moving else unreal.ComponentMobility.STATIC)
 c.set_static_mesh(unreal.load_asset(ROOT+'/Meshes/SM_'+name));a.set_actor_location(vec(location),False,True)
 if rotation:a.set_actor_rotation(rotation,True)
 return a,c

terrain,tc=mesh_actor('Temperate Shelf | Sand channels and rocky bank','ShelfTerrain',(0,0,0))
tc.set_collision_profile_name('BlockAll');prop(tc,'cast_shadow',True)
old=next(a for a in EAS.get_all_level_actors() if a.get_actor_label()=='Seabed')
old.set_actor_hidden_in_game(True)
old.get_component_by_class(unreal.StaticMeshComponent).set_visibility(False)
old.get_component_by_class(unreal.StaticMeshComponent).set_collision_profile_name('NoCollision')

containers={};counts={};rocks=[];planted=[]
def instances(name,solid=False):
 if name in containers:return containers[name]
 a=actor('Ecology | '+name)
 c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
 if not c:c=A.add_component(a,unreal.HierarchicalInstancedStaticMeshComponent.static_class(),'EcologyInstances')
 c.clear_instances();c.set_static_mesh(unreal.load_asset(ROOT+'/Meshes/SM_'+name))
 prop(c,'mobility',unreal.ComponentMobility.STATIC)
 prop(c,'cast_shadow',solid or name.startswith('GiantKelp'))
 prop(c,'instance_start_cull_distance',8000 if solid else 5500)
 prop(c,'instance_end_cull_distance',15000 if solid else 9000)
 prop(c,'generate_overlap_events',False)
 c.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
 containers[name]=c;counts[name]=0;return c
def place(name,x,y,z,scale=(1,1,1),yaw=None,solid=False,pitch=0,roll=0):
 c=instances(name,solid)
 c.add_instance(unreal.Transform(location=unreal.Vector(x,y,z),rotation=unreal.Rotator(pitch=pitch,yaw=R.uniform(0,360) if yaw is None else yaw,roll=roll),scale=vec(scale)),world_space=True)
 counts[name]+=1
def ground(x,y,above=700):
 z=height(x,y)
 hit=unreal.SystemLibrary.line_trace_single(w,unreal.Vector(x,y,z+above),unreal.Vector(x,y,z-400),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE)
 if hit and hit.to_tuple()[0]:return hit.to_tuple()[4].z
 return z

# Fractured reef outcrops. All positions in actual Unreal world coordinates.
for cx,cy,radius in REEFS:
 for j in range(20):
  a=R.random()*math.tau;r=radius*math.sqrt(R.random());x=cx+math.cos(a)*r;y=cy+math.sin(a)*r
  if reserved(x,y,220):continue
  sc=R.uniform(.65,2.2);z=height(x,y)+18*sc
  place('ReefRock'+str(j%4),x,y,z,(sc*R.uniform(1,1.5),sc,sc),solid=True,pitch=R.uniform(-8,8),roll=R.uniform(-6,6))
  rocks.append((x,y,sc))

# Kelp holdfasts and understory attach to reef crowns, never to deep mud.
for j,(x,y,sc) in enumerate(rocks):
 z=ground(x,y)
 if z>-3200 and j%2==0:
  variant=2 if z<-2300 else 1
  H=(700,1250,1850)[variant];size=R.uniform(.7,1.05)
  size=min(size,(-z-300)/H)
  if not reserved(x,y,330):
   place('GiantKelp'+str(variant),x,y,z-3,(size,size,size));planted.append((x,y,z,H*size))
 for k in range(3):
  xx=x+R.uniform(-45,45)*sc;yy=y+R.uniform(-45,45)*sc;zz=ground(xx,yy)
  if zz>-3500:place('UnderstoryKelp' if k<2 else 'RedAlgae',xx,yy,zz-1,(R.uniform(.6,1.2),)*3)
 if j%3==0:place('Urchin',x+20,y,z+3,(R.uniform(.7,1.3),)*3)
 if j%5==0:place('MusselCluster',x-30,y,z+2,(R.uniform(.8,1.5),)*3)

# Low scattered algae at reef margins, with many unplanted sand gaps.
for cx,cy,radius in REEFS:
 for j in range(65):
  a=R.random()*math.tau;r=radius*math.sqrt(R.random());x=cx+r*math.cos(a);y=cy+r*math.sin(a)
  if reserved(x,y,180) or height(x,y)<-3300:continue
  z=ground(x,y)
  # Low algae on rubble; not tall kelp rooted directly in sand.
  if j%3==0:
   place('ReefRock'+str(j%4),x,y,height(x,y),(0.22,.25,.12),solid=False)
   place('RedAlgae',x,y,z+8,(R.uniform(.6,1.3),)*3)

# A sheltered, higher sandy pocket supports a modest seagrass meadow.
grass_count=0
for j in range(600):
 x=R.uniform(-1700,1700);y=R.uniform(4400,7800)
 if height(x,y)>-1200 and R.random()<.6:
  place('Eelgrass',x,y,height(x,y)-2,(R.uniform(.7,1.2),)*3);grass_count+=1
for j in range(50):
 x=R.uniform(1500,12000);y=R.uniform(-4500,6000)
 if not reserved(x,y,160) and height(x,y)>-4500:
  place('SeaStar',x,y,ground(x,y)+2,(R.uniform(.65,1.3),)*3)

# School geometry is offset from its actor pivot and faces the orbital tangent.
# Stock RotatingMovement supplies slow, stable movement without editor callbacks.
paths=[(4200,-800,1150,'SilverShoal',-3.3),(10500,700,1000,'SilverShoal',-4.1),(7100,4300,1250,'SilverShoal',-2.8),(2600,4500,1050,'SilverShoal',-3.8),(10100,-3400,1200,'SilverShoal',-3.2),(4700,1600,480,'RockfishPair',-2.2),(10000,4500,420,'RockfishPair',-1.7),(2800,-2300,330,'Skate',-1.3),(11900,2500,280,'Skate',-1.5)]
for i,(x,y,clearance,name,speed) in enumerate(paths):
 radius=1050 if name=='SilverShoal' else 850 if name=='Skate' else 650
 highest=max(height(x+radius*math.cos(j*math.tau/24),y+radius*math.sin(j*math.tau/24)) for j in range(24))
 z=min(-300,highest+clearance)
 a,c=mesh_actor('Wildlife | '+name+' '+str(i),name,(x,y,z),unreal.Rotator(),True)
 c.set_collision_profile_name('NoCollision');prop(c,'cast_shadow',False)
 movement=a.get_component_by_class(unreal.RotatingMovementComponent)
 if not movement:movement=A.add_component(a,unreal.RotatingMovementComponent.static_class(),'NaturalSwimming')
 prop(movement,'rotation_rate',unreal.Rotator(pitch=0,yaw=speed,roll=0));prop(movement,'rotation_in_local_space',True)
 prop(movement,'auto_activate',True);prop(movement,'auto_register_updated_component',True)
 movement.set_updated_component(a.get_component_by_class(unreal.StaticMeshComponent))

# A previous editor-placed diver stood below the newly raised shelf. Move only
# that invalid starting position into the open water beside the habitat.
for a in EAS.get_all_level_actors():
 if a.get_class().get_name()=='BP_DiverPawn_C':
  p=a.get_actor_location()
  if p.z<height(p.x,p.y)+100:
   a.set_actor_location(unreal.Vector(8900,2900,-2500),False,True)
   a.set_actor_rotation(unreal.Rotator(pitch=-8,yaw=-120,roll=0),True)
   unreal.log('ECOLOGY_DIVER_START_CLEARED moved above new terrain')

unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
with open(os.path.join(os.path.dirname(__file__),'installed_layout.json'),'w') as f:json.dump({'instances':counts,'kelp':planted,'swimming_groups':len(paths),'seagrass':grass_count},f,indent=2)
unreal.log('ECOLOGY_INSTALL_SUCCESS '+str(counts)+' swimming_groups='+str(len(paths)))
