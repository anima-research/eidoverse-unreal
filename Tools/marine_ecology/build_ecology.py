"""Original modeled temperate marine kit, Blender background. No downloaded assets."""
import bpy,bmesh,math,random,json,os,sys
from mathutils import Vector
sys.path.insert(0,os.path.dirname(__file__))
from ecology_layout import height
R=random.Random(9126)
OUT=os.path.join(os.path.dirname(__file__),'exports');os.makedirs(OUT,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=.01
palette={'Sand':((.25,.235,.18),0,.93),'Rock':((.13,.155,.145),0,.87),'Kelp':((.14,.17,.052),0,.57),'RedAlgae':((.20,.074,.055),0,.72),'Grass':((.11,.19,.065),0,.7),'Silver':((.47,.54,.53),.38,.3),'Back':((.045,.095,.105),.22,.4),'Fin':((.15,.21,.20),.1,.48),'Eye':((.003,.007,.008),.1,.19),'Rockfish':((.32,.19,.105),.08,.65),'Ray':((.19,.22,.18),.05,.72),'Urchin':((.10,.04,.065),0,.83),'Star':((.34,.17,.11),0,.85),'Mussel':((.027,.047,.061),.1,.43)}
mats={}
for n,(rgb,met,rough) in palette.items():
 m=bpy.data.materials.new('E_'+n);m.diffuse_color=(*rgb,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*rgb,1);p.inputs['Metallic'].default_value=met;p.inputs['Roughness'].default_value=rough
 mats[n]=m
groups={};group='';animate='none'
def add(o,material,smooth=True):
 o.data.materials.append(mats[material]);groups.setdefault(group,[]).append(o)
 if smooth:
  for f in o.data.polygons:f.use_smooth=True
 return o
def mesh(name,v,f,material,weights=None):
 me=bpy.data.meshes.new(name);me.from_pydata(v,[],f);me.update();o=bpy.data.objects.new(name,me);bpy.context.collection.objects.link(o);add(o,material)
 col=me.color_attributes.new(name='EcologyMotion',type='FLOAT_COLOR',domain='POINT')
 for i,vert in enumerate(me.vertices):
  w=weights[i] if weights else (0,0,0,1)
  col.data[i].color=w
 return o
def sphere(name,p,scale,material,seg=16,rings=8):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=seg,ring_count=rings,radius=1,location=p);o=bpy.context.object;o.name=name;o.scale=scale
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 return add(o,material)
def tube(name,points,radii,material,sides=7):
 v=[];f=[]
 for i,p in enumerate(points):
  d=Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)]);d.normalize()
  u=d.cross(Vector((0,0,1)))
  if u.length<.01:u=d.cross(Vector((0,1,0)))
  u.normalize();vv=d.cross(u)
  for j in range(sides):v.append(tuple(Vector(p)+radii[i]*(u*math.cos(j*math.tau/sides)+vv*math.sin(j*math.tau/sides))))
 for i in range(len(points)-1):
  for j in range(sides):f.append((i*sides+j,i*sides+(j+1)%sides,(i+1)*sides+(j+1)%sides,(i+1)*sides+j))
 f.extend([tuple(range(sides-1,-1,-1)),tuple((len(points)-1)*sides+j for j in range(sides))])
 return mesh(name,v,f,material)
def ribbon(name,points,widths,material,phase=0):
 v=[];f=[]
 for i,p in enumerate(points):
  d=Vector(points[min(i+1,len(points)-1)])-Vector(points[max(0,i-1)])
  side=d.cross(Vector((0,0,1)))
  if side.length<.01:side=Vector((1,0,0))
  side.normalize()
  for j in (-1,0,1):v.append(tuple(Vector(p)+side*(widths[i]*j)+Vector((0,0,(1-abs(j))*widths[i]*.22))))
 for i in range(len(points)-1):
  for j in range(2):f.append((i*3+j,i*3+j+1,(i+1)*3+j+1,(i+1)*3+j))
 return mesh(name,v,f,material)

# A continuous shelf descending into the original ~200 m basin. Export Y is
# mirrored by the Unreal FBX pipeline, so sample desired Unreal y at -Blender y.
group='ShelfTerrain'
coords=sorted(set(list(range(-18000,30001,200))+list(range(-65000,-18000,1600))+list(range(30000,75001,1600))))
v=[(x,y,height(x,-y)) for y in coords for x in coords];n=len(coords)
f=[(j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i) for j in range(n-1) for i in range(n-1)]
mesh(group,v,f,'Sand')

for k in range(4):
 group=f'ReefRock{k}'
 bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3,radius=1);o=bpy.context.object
 for vert in o.data.vertices:
  p=vert.co;rad=1+.12*math.sin(p.x*11+k)+.07*math.sin(p.y*17+p.z*5)
  p.x*=rad*R.uniform(135,155);p.y*=rad*110;p.z*=rad*95
  p.z=max(p.z,-65)
 add(o,'Rock')

for variant,H in enumerate((700,1250,1850)):
 group=f'GiantKelp{variant}'
 for j in range(9):
  a=j*math.tau/9;tube('Branching holdfast',[(0,0,30),(math.cos(a)*28,math.sin(a)*28,8),(math.cos(a)*55,math.sin(a)*55,0)],[3,2,1],'Kelp')
 for stem in range(2):
  phase=variant+stem*1.7
  pts=[(45*math.sin(i/9*2+phase)*(i/9),25*math.sin(i/9*3+phase)*(i/9),i/9*H) for i in range(10)]
  tube('Flexible stipe',pts,[3.2-i*.25 for i in range(10)],'Kelp')
  for j in range(3,int(H/55)):
   z=j*55;u=z/H;a=j*2.4+phase;x=45*math.sin(u*2+phase)*u;y=25*math.sin(u*3+phase)*u
   reach=R.uniform(95,185);pts=[];width=[]
   for q in range(9):
    t=q/8;pts.append((x+math.cos(a)*reach*t,y+math.sin(a)*reach*t,z+35*math.sin(t*math.pi)-reach*.45*t*t));width.append(13*math.sin(math.pi*t)**.65+.3)
   sphere('Pneumatocyst',(x+math.cos(a)*12,y+math.sin(a)*12,z),(4,4,7),'Kelp',8,4)
   ribbon('Ruffled kelp blade',pts,width,'Kelp')
 # Encode anchor-safe current strength in vertex colors after joining below.

for species in ('UnderstoryKelp','RedAlgae','Eelgrass'):
 group=species;material={'UnderstoryKelp':'Kelp','RedAlgae':'RedAlgae','Eelgrass':'Grass'}[species]
 for j in range(18 if species=='Eelgrass' else 12):
  a=R.random()*math.tau;length=R.uniform(65,135) if species!='RedAlgae' else R.uniform(25,60)
  points=[];widths=[]
  for q in range(9):
   t=q/8;spread=length*(.55 if species!='Eelgrass' else .22)*t*t
   points.append((math.cos(a)*spread+math.sin(j)*8,math.sin(a)*spread+math.cos(j)*8,length*t*(1-.15*t)))
   widths.append((8 if species=='UnderstoryKelp' else 3 if species=='RedAlgae' else 1.1)*math.sin(math.pi*t)**.6+.1)
  ribbon(species,points,widths,material)

def fish(center,length,heading,material='Silver',phase=0):
 # Head +X, proper forked caudal fin, paired pectoral fins, dorsal/ventral fins.
 v=[];f=[];weights=[];rings=[(-.5,.02),(-.38,.09),(-.2,.16),(0,.18),(.23,.15),(.4,.09),(.5,.015)]
 co,si=math.cos(heading),math.sin(heading)
 def tr(p):return (center[0]+co*p[0]-si*p[1],center[1]+si*p[0]+co*p[1],center[2]+p[2])
 for x,r in rings:
  for j in range(12):
   a=j*math.tau/12;v.append(tr((x*length,math.cos(a)*r*length*.55,math.sin(a)*r*length)))
   weights.append((max(0,-x*2)**1.5,(co+1)/2,(si+1)/2,phase))
 for i in range(len(rings)-1):
  for j in range(12):f.append((i*12+j,i*12+(j+1)%12,(i+1)*12+(j+1)%12,(i+1)*12+j))
 o=mesh('Streamlined fish',v,f,material,weights)
 o.data.materials.append(mats['Back'])
 for poly in o.data.polygons:
  if poly.center.z>center[2]+length*.04:poly.material_index=1
 fins=[([(-.48,0,0),(-.76,0,.20),(-.64,0,0),(-.76,0,-.20)],[(0,1,2),(0,2,3)]),([(-.15,0,.15),(-.15,0,.31),(.14,0,.16)],[(0,1,2)]),([(-.06,0,-.13),(-.17,0,-.23),(.1,0,-.15)],[(0,1,2)])]
 for side in (-1,1):fins.append(([(.2,side*.075,-.02),(-.06,side*.25,-.1),(.02,side*.055,-.09)],[(0,1,2)]))
 for fv,ff in fins:mesh('Paired fin', [tr(tuple(c*length for c in p)) for p in fv],ff,'Fin',[(max(0,-p[0]*2)**1.5,(co+1)/2,(si+1)/2,phase) for p in fv])
 for side in (-1,1):
  sphere('Dark eye',tr((length*.335,side*length*.068,length*.04)),(length*.027,)*3,'Eye',8,4)

group='SilverShoal'
for j in range(32):
 x=R.uniform(580,930);y=R.uniform(-230,230);z=R.uniform(-95,95)
 fish((x,y,z),R.uniform(19,29),math.pi/2+R.uniform(-.12,.12),phase=R.random())
group='RockfishPair'
for j in range(3):fish((350+j*75,R.uniform(-70,70),R.uniform(-25,25)),R.uniform(36,49),math.pi/2,material='Rockfish',phase=R.random())
group='Skate'
# Rounded diamond with a raised head/spine and flexible pointed wings.
v=[(0,0,6)];f=[]
for j in range(32):
 a=j*math.tau/32;v.append((math.cos(a)*50,math.sin(a)*75,1+4*math.cos(a)))
for j in range(32):f.append((0,j+1,(j+1)%32+1))
mesh('Skate dorsal disc',v,f,'Ray',[(abs(p[1])/75,0,0,1) for p in v])
tube('Slender skate tail',[(-35,0,3),(-70,0,0),(-105,5,-3),(-145,10,-2)],[6,3,1.6,.4],'Ray')
for y in (-9,9):sphere('Raised skate eye',(26,y,8),(3,3,3),'Eye',8,4)

group='Urchin'
sphere('Urchin test',(0,0,6),(9,9,6),'Urchin')
for j in range(72):
 a=j*2.399963;z=R.uniform(.05,1);r=math.sqrt(1-z*z);direction=Vector((r*math.cos(a),r*math.sin(a),z))
 p=Vector((0,0,5))+direction*7;q=p+direction*R.uniform(5,11)
 tube('Urchin spine',[tuple(p),tuple(q)],[.6,.08],'Urchin',4)
group='SeaStar'
v=[(0,0,5)];f=[]
for j in range(40):
 a=j*math.tau/40;rad=9+13*((1+math.cos(a*5))*.5)**2
 v.append((math.cos(a)*rad,math.sin(a)*rad,1.5))
for j in range(40):f.append((0,j+1,(j+1)%40+1))
mesh('Five tapered arms',v,f,'Star')
for j in range(35):
 a=R.random()*math.tau;r=R.uniform(1,8);sphere('Granular skin',(r*math.cos(a),r*math.sin(a),4),(.6,.6,.6),'Star',6,3)
group='MusselCluster'
for j in range(18):
 o=sphere('Blue mussel valve',(R.uniform(-15,15),R.uniform(-15,15),R.uniform(2,5)),(3,7,3),'Mussel',10,5);o.rotation_euler.z=R.random()*math.pi

# Expansion meshes keep the original kit intact; individual fish face +X.
for n,rgb,met,rough in [('Mackerel',(.20,.33,.32),.35,.36),('Perch',(.36,.29,.16),.12,.5),('Deck',(.20,.23,.22),.55,.68),('Galvanized',(.40,.44,.43),.8,.4),('Safety',(.64,.38,.045),.2,.55),('Rubber',(.022,.028,.028),0,.85),('Sponge',(.31,.22,.10),0,.86),('Anemone',(.39,.23,.17),0,.65),('Cucumber',(.14,.10,.075),0,.9),('Shell',(.44,.39,.30),0,.82)]:
 palette[n]=(rgb,met,rough);m=bpy.data.materials.new('E_'+n);m.diffuse_color=(*rgb,1);mats[n]=m
for name,length,material,slender in [('Sardine',26,'Silver',.7),('Mackerel',56,'Mackerel',.72),('ReefPerch',38,'Perch',1.35)]:
 group=name;fish((0,0,0),length,0,material)
 for o in groups[group]:
  for vv in o.data.vertices:vv.co.z*=slender
 if name=='Mackerel':
  for j in range(5):
   x=-length*(.24+j*.05)
   mesh('Dorsal finlet',[(x,0,4),(x-2,0,7),(x-4,0,3)],[(0,1,2)],'Fin')
 if name=='ReefPerch':
  for j in range(8):
   x=-11+j*3
   mesh('Spiny dorsal ray',[(x,0,7),(x-1,0,13),(x+3,0,8)],[(0,1,2)],'Perch')
group='IndividualSkate'
for original in groups['Skate']:
 copy=original.copy();copy.data=original.data.copy();bpy.context.collection.objects.link(copy);groups.setdefault(group,[]).append(copy)

group='Crab'
sphere('Low oval carapace',(0,0,8),(15,12,6),'Rockfish')
for side in (-1,1):
 for j in range(4):
  x=8-j*6;tube('Jointed walking leg',[(x,side*8,7),(x-6,side*20,11),(x-12,side*30,1)],[1.7,1.2,.3],'Rockfish',5)
 tube('Raised claw arm',[(10,side*7,7),(22,side*15,10),(29,side*12,13)],[2,2,2.7],'Rockfish')
 sphere('Pincer palm',(30,side*12,13),(5,3,3),'Rockfish',10,5)
 for a in (-1,1):tube('Pincer finger',[(32,side*12+a*2,13),(38,side*12+a*1.5,13),(38,side*12,13)],[1.2,.7,.2],'Rockfish',5)
 sphere('Stalk eye',(13,side*5,14),(1.4,1.4,1.4),'Eye',8,4)
group='SeaCucumber'
o=sphere('Leathery cucumber',(0,0,7),(29,9,8),'Cucumber',24,10)
for j in range(45):
 x=R.uniform(-23,23);a=R.uniform(.2,math.pi-.2);tube('Soft papilla',[(x,math.cos(a)*8,7+math.sin(a)*7),(x+1,math.cos(a)*10,7+math.sin(a)*10)],[1,.2],'Cucumber',4)
group='AnemoneCluster'
for k in range(4):
 x,y=R.uniform(-20,20),R.uniform(-20,20);H=R.uniform(12,26)
 tube('Anemone column',[(x,y,0),(x,y,H*.7),(x,y,H)],[7,5,8],'Anemone',12)
 for j in range(24):
  a=j*math.tau/24;rad=R.uniform(10,17)
  tube('Flexible tentacle',[(x+math.cos(a)*6,y+math.sin(a)*6,H),(x+math.cos(a)*rad,y+math.sin(a)*rad,H+8),(x+math.cos(a)*rad*1.1,y+math.sin(a)*rad*1.1,H+4)],[1,.6,.1],'Anemone',4)
group='SpongeCluster'
for k in range(5):
 x,y=R.uniform(-23,23),R.uniform(-23,23);H=R.uniform(18,50);rad=R.uniform(5,10)
 # Open osculum: outer/inner rings share a rim, not capped cylinders.
 v=[];f=[]
 for z,r in [(0,rad*.8),(H*.5,rad),(H,rad*.75),(H,rad*.52),(H*.25,rad*.45)]:
  for j in range(14):v.append((x+math.cos(j*math.tau/14)*r,y+math.sin(j*math.tau/14)*r,z))
 for q in range(4):
  for j in range(14):f.append((q*14+j,q*14+(j+1)%14,(q+1)*14+(j+1)%14,(q+1)*14+j))
 mesh('Porous finger sponge',v,f,'Sponge')
group='ShellHash'
for k in range(24):
 o=sphere('Broken shell',(R.uniform(-70,70),R.uniform(-70,70),R.uniform(0,2)),(R.uniform(1.5,4),R.uniform(2,6),.5),'Shell',8,4);o.rotation_euler=(R.uniform(-.4,.4),R.uniform(-.3,.3),R.random()*math.tau)
group='BeddingRock'
for j in range(4):
 bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2,radius=1);o=bpy.context.object
 for vv in o.data.vertices:
  p=vv.co;p.x=p.x*R.uniform(170,240)+j*75;p.y*=R.uniform(90,140);p.z=p.z*28+j*28
 add(o,'Rock',False)

def box(name,p,scale,material,bevel=0):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.name=name;o.scale=scale;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);add(o,material,False)
 if bevel:
  mod=o.modifiers.new('Manufactured edges','BEVEL');mod.width=bevel;mod.segments=2;bpy.context.view_layer.objects.active=o;bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
group='ResearchPlatform'
# 12 x 10 m piled steel research stage. Local Blender Y mirrors into Unreal.
for x in (-510,510):
 for y in (-410,410):
  floor=height(x,6000-y)-40
  tube('Seabed pile',[(x,y,floor),(x,y,225)],[22,22],'Galvanized',16)
  tube('Splash-zone sleeve',[(x,y,-110),(x,y,120)],[25,25],'Rubber',16)
for x in (-510,510):box('Longitudinal I beam',(x,0,190),(25,1000,65),'Galvanized',2)
for y in (-410,0,410):box('Cross member',(0,y,202),(1200,22,42),'Galvanized',2)
for j in range(24):box('Anti-slip deck panel',(-575+j*50,0,230),(48.5,1000,20),'Deck',.8)
# Guardrails with a deliberate ladder gate on the south face (Blender +Y).
for x in (-590,590):
 for y in range(-480,481,240):tube('Guardrail upright',[(x,y,241),(x,y,345)],[3,3],'Galvanized',8)
 for z in (292,345):tube('Side safety rail',[(x,-480,z),(x,480,z)],[3,3],'Galvanized',8)
for y in (-490,490):
 for x in (-570,-300,-85,85,300,570):tube('End upright',[(x,y,241),(x,y,345)],[3,3],'Galvanized',8)
 for z in (292,345):
  if y<0:tube('Rear safety rail',[(-590,y,z),(590,y,z)],[3,3],'Galvanized',8)
  else:
   for side in (-1,1):tube('Ladder gate rail',[(side*85,y,z),(side*590,y,z)],[3,3],'Safety',8)
# Ladder extends 1.8 m underwater, with curved grab handles and 28 cm rung spacing.
for x in (-65,65):tube('Boarding ladder stringer',[(x,610,-180),(x,610,300),(x,590,354),(x,535,365),(x,490,330),(x,490,242)],[4]*6,'Safety',12)
for z in range(-160,241,28):tube('Non-slip ladder rung',[(-65,610,z),(65,610,z)],[3.5,3.5],'Galvanized',10)
for x in (-510,510):tube('Underwater cross brace',[(x,-410,-300),(x,410,130)],[7,7],'Galvanized',10)
for x in (-370,370):
 box('Mooring cleat foot',(x,430,247),(45,24,14),'Galvanized',3);tube('Mooring cleat horns',[(x-36,430,262),(x+36,430,262)],[5,5],'Galvanized',10)
# Small instrument stand leaves the deck and boarding route open.
for x in (-460,-160):tube('Instrument canopy post',[(x,-330,240),(x,-330,495)],[5,5],'Galvanized',10)
box('Weather canopy',(-310,-330,502),(390,250,12),'Deck',3)
box('Instrument enclosure',(-420,-330,320),(85,65,140),'Deck',5)
sphere('Amber navigation lamp',(-460,-330,531),(10,10,14),'Safety',12,6)

manifest=[]
for name,objects in groups.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 if len(objects)>1:bpy.ops.object.join()
 o=bpy.context.object;o.name='SM_'+name
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR');bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 if name=='Skate':
  for vv in o.data.vertices:
   x,y=vv.co.x,vv.co.y;vv.co.x=700-y;vv.co.y=x
 # Consistent closed-solid winding; open blades deliberately remain two-sided.
 if name.startswith('ReefRock'):
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(o.data);bm.free()
 if name.startswith('GiantKelp') or name in ('UnderstoryKelp','RedAlgae','Eelgrass'):
  col=o.data.color_attributes.get('EcologyMotion') or o.data.color_attributes.new(name='EcologyMotion',type='FLOAT_COLOR',domain='POINT')
  top=max(v.co.z for v in o.data.vertices)
  for i,vv in enumerate(o.data.vertices):col.data[i].color=(max(0,vv.co.z/top)**1.4,0,0,1)
 mod=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
 target=os.path.join(OUT,o.name+'.fbx')
 bpy.ops.export_scene.fbx(filepath=target,use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,add_leaf_bones=False,mesh_smooth_type='FACE',use_mesh_modifiers=True,colors_type='LINEAR')
 manifest.append({'name':name,'mesh':o.name,'file':target,'triangles':len(o.data.polygons)})
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'TemperateShelf.blend'))
with open(os.path.join(OUT,'manifest.json'),'w') as f:json.dump({'palette':palette,'meshes':manifest},f,indent=2)
print('ECOLOGY_BUILD_SUCCESS',json.dumps(manifest))
