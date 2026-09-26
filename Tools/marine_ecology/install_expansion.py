"""Biome-aware instanced scatter, independent wildlife and boardable platform."""
import unreal,math,random,sys,os,json
sys.path.insert(0,os.path.dirname(__file__))
from ecology_layout import height,reserved,REEFS
from editor_toolset.toolsets.actor import ActorTools as A
EAS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world.get_name()=='L_Epipelagic', 'Load L_Epipelagic before installing'
ROOT='/Game/Underwater/Ecology/Meshes/SM_';R=random.Random(9182)
def actor(label,cls=unreal.Actor):
 a=next((a for a in EAS.get_all_level_actors() if a.get_actor_label()==label),None)
 if not a:a=EAS.spawn_actor_from_class(cls,unreal.Vector())
 a.set_actor_label(label);a.set_folder_path('Marine Ecology/Expansion');a.set_editor_property('tags',['MarineEcology','EcologyExpansion']);return a
def ground(x,y):
 z=height(x,y)
 hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,z+650),unreal.Vector(x,y,z-400),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE)
 if hit and hit.to_tuple()[0]:
  data=hit.to_tuple();return data[4].z,data[6]
 return z,unreal.Vector(0,0,1)
cs={};counts={};placements=[]
# Remove this pass's old instance transforms before any terrain traces. Otherwise
# re-running the tool would stack new rocks and fauna on their previous copies.
for a in EAS.get_all_level_actors():
 if a.get_actor_label().startswith('Biome detail |'):
  c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  if c:c.clear_instances()
def place(name,x,y,scale=1,solid=False):
 if reserved(x,y,200) or (abs(x)<800 and abs(y-6000)<800):return
 z,n=ground(x,y)
 if n.z<.72:return
 if name not in cs:
  a=actor('Biome detail | '+name);c=a.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
  if not c:c=A.add_component(a,unreal.HierarchicalInstancedStaticMeshComponent.static_class(),'BiomeInstances')
  c.clear_instances();c.set_static_mesh(unreal.load_asset(ROOT+name));c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
  c.set_editor_property('cast_shadow',solid);c.set_collision_profile_name('BlockAll' if solid else 'NoCollision')
  c.set_editor_property('instance_start_cull_distance',9000 if solid else 3500)
  c.set_editor_property('instance_end_cull_distance',16000 if solid else 6500)
  cs[name]=c;counts[name]=0
 yaw=R.uniform(0,360)
 # Align bottom life to the traced seabed tangent. Preserve random azimuth.
 rot=unreal.MathLibrary.make_rot_from_z(n)
 q=rot.quaternion()*unreal.Rotator(pitch=0,yaw=yaw,roll=0).quaternion()
 cs[name].add_instance(unreal.Transform(location=unreal.Vector(x,y,z-1),rotation=q.rotator(),scale=unreal.Vector(scale,scale,scale)),world_space=True)
 counts[name]+=1;placements.append((name,x,y,z))

# Retire rigid school loops without deleting the original actors/assets.
for a in EAS.get_all_level_actors():
 if a.get_actor_label().startswith('Wildlife |'):
  a.set_actor_hidden_in_game(True);a.set_actor_enable_collision(False)
  c=a.get_component_by_class(unreal.RotatingMovementComponent)
  if c:c.set_editor_property('auto_activate',False)
eco=actor('Marine ecosystem | Independent schooling',unreal.MarineEcosystem)
eco.set_editor_property('species_meshes',[unreal.load_asset(ROOT+n) for n in ('Sardine','Mackerel','ReefPerch','IndividualSkate')])

# Bedrock escarpments and sessile filter feeders on the outer shelf.
for cx,cy,radius in REEFS+[(13500,5000,1800),(-2800,2200,1500),(4800,9800,2000)]:
 for j in range(34):
  a=R.random()*math.tau;r=radius*math.sqrt(R.random());x=cx+math.cos(a)*r;y=cy+math.sin(a)*r
  if j%4==0:place('BeddingRock',x,y,R.uniform(.55,1.65),True)
  if j%3==0:place('SpongeCluster',x,y,R.uniform(.65,1.2))
  if j%4==0:place('AnemoneCluster',x+20,y-20,R.uniform(.65,1.3))
  if j%5==0:place('Crab',x+70,y+30,R.uniform(.65,1.4))
  if j%2==0:place('RedAlgae',x-50,y+80,R.uniform(.7,1.6))

# Coarse shell beds at current-swept transition zones, in irregular patches.
for cx,cy in [(2600,6000),(6000,5200),(10500,-1700),(3500,-3300),(11000,6800)]:
 for j in range(100):
  x=cx+R.gauss(0,700);y=cy+R.gauss(0,500)
  place('ShellHash',x,y,R.uniform(.6,1.5))
  if j%5==0:place('ReefRock'+str(j%4),x,y,R.uniform(.08,.23))
  if j%13==0:place('SeaCucumber',x,y,R.uniform(.8,1.3))

# Deeper aphotic/sparse silt community: no implausible deep seagrass.
for j in range(180):
 x=R.uniform(-6000,16000);y=R.uniform(8500,17000)
 if height(x,y)>-3800:continue
 place('SeaCucumber' if j%3 else 'SeaStar',x,y,R.uniform(.8,1.5))
 if j%7==0:place('SpongeCluster',x+25,y+30,R.uniform(.7,1.1))

# Patchier shallow meadow, kept clear of platform piles and ladder.
for j in range(450):
 x=R.uniform(-2700,1900);y=R.uniform(4100,8500)
 if height(x,y)<-1450:continue
 density=(math.sin(x*.0018+math.sin(y*.001))*math.cos(y*.0014)+1)*.5
 if R.random()>density:continue
 place('Eelgrass',x,y,R.uniform(.7,1.5))

platform=actor('Research platform | E to climb ladder',unreal.SurfaceBoardingPlatform)
platform.set_actor_location(unreal.Vector(0,6000,0),False,True)
c=platform.get_component_by_class(unreal.StaticMeshComponent)
if not c:c=A.add_component(platform,unreal.StaticMeshComponent.static_class(),'PiledResearchStage')
c.set_static_mesh(unreal.load_asset(ROOT+'ResearchPlatform'));c.set_collision_profile_name('BlockAll');c.set_editor_property('cast_shadow',True)
c.set_editor_property('mobility',unreal.ComponentMobility.STATIC)
# Legible identity and boarding hint on the deck, rather than a floating UI prop.
text=platform.get_component_by_class(unreal.TextRenderComponent)
if not text:text=A.add_component(platform,unreal.TextRenderComponent.static_class(),'StationMarking')
text.set_text('SHELF RESEARCH  /  02');text.set_world_size(22)
text.set_text_render_color(unreal.Color(200,190,150,255))
text.set_relative_location(unreal.Vector(-330,-270,241),False,True);text.set_relative_rotation(unreal.Rotator(pitch=90,yaw=90,roll=0),False,True)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
with open(os.path.join(os.path.dirname(__file__),'expansion_layout.json'),'w') as f:json.dump({'instances':counts,'placements':placements,'platform':[0,6000,0],'ladder_foot':[0,5395,-100],'deck_height':240},f,indent=2)
unreal.log('ECOLOGY_EXPANSION_INSTALL_SUCCESS '+str(counts))
