"""Authored pressure-vessel kit. Blender background script; all dimensions cm.
Exports independent collision shell, fittings, interior, glazing and lettering meshes.
"""
import bpy, bmesh, math, os, json
from mathutils import Vector
from collections import defaultdict

OUT=os.path.join(os.path.dirname(__file__),'exports')
os.makedirs(OUT,exist_ok=True)
bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC'
bpy.context.scene.unit_settings.scale_length=.01
palette={
 'Ceramic':((.72,.78,.74),.35,.33), 'Teal':((.025,.16,.18),.65,.3),
 'Graphite':((.035,.055,.065),.7,.38), 'Titanium':((.28,.35,.37),.85,.25),
 'Safety':((.95,.23,.035),.25,.38), 'Interior':((.66,.59,.44),.1,.62),
 'Rubber':((.012,.022,.025),.05,.8), 'Glow':((.12,.72,.82),.1,.25),
 'Warm':((1,.65,.25),.1,.25), 'Glass':((.075,.23,.28),.05,.12),
}
materials={}
for name,(rgb,metal,rough) in palette.items():
 m=bpy.data.materials.new('V_'+name);m.diffuse_color=(*rgb,1);m.use_nodes=True
 p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*rgb,1)
 p.inputs['Metallic'].default_value=metal;p.inputs['Roughness'].default_value=rough
 if name in ('Glow','Warm'):p.inputs['Emission Color'].default_value=(*rgb,1);p.inputs['Emission Strength'].default_value=2
 materials[name]=m
groups=defaultdict(list)
vessel='Habitat'
def collect(o,mat,group='Fittings',smooth=False):
 o.data.materials.append(materials[mat]);groups[vessel+'_'+group].append(o)
 if smooth and o.type=='MESH':
  for p in o.data.polygons:p.use_smooth=True
 return o
def bevel(o,width=2,segments=3):
 mod=o.modifiers.new('Manufactured radii','BEVEL');mod.width=width;mod.segments=segments
 bpy.context.view_layer.objects.active=o
 bpy.ops.object.modifier_apply(modifier=mod.name)
 return o
def box(name,p,size,mat='Graphite',group='Fittings',radius=2,rot=None):
 bpy.ops.mesh.primitive_cube_add(size=1,location=p);o=bpy.context.object;o.name=name;o.dimensions=size
 bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
 if radius:bevel(o,min(radius,min(size)/3))
 if rot:o.rotation_euler=rot
 return collect(o,mat,group)
def cylinder(name,p,r,depth,mat='Titanium',axis='Z',group='Fittings',vertices=40):
 bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=r,depth=depth,location=p)
 o=bpy.context.object;o.name=name
 if axis=='X':o.rotation_euler[1]=math.pi/2
 if axis=='Y':o.rotation_euler[0]=math.pi/2
 bevel(o,min(2,r*.08));return collect(o,mat,group,True)
def sphere(name,p,size,mat='Ceramic',group='Fittings'):
 bpy.ops.mesh.primitive_uv_sphere_add(segments=40,ring_count=20,radius=1,location=p)
 o=bpy.context.object;o.name=name;o.scale=size;return collect(o,mat,group,True)
def pipe(name,points,r=3,mat='Titanium',group='Fittings',closed=False):
 curve=bpy.data.curves.new(name,'CURVE');curve.dimensions='3D';curve.bevel_depth=r;curve.bevel_resolution=3
 sp=curve.splines.new('POLY');sp.points.add(len(points)-1)
 for p,co in zip(sp.points,points):p.co=(*co,1)
 sp.use_cyclic_u=closed;o=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(o)
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH')
 return collect(bpy.context.object,mat,group,True)
def ring(name,x,ry,rz,cz=0,r=5,mat='Titanium',group='Fittings',lo=0,hi=2*math.pi):
 return pipe(name,[(x,ry*math.cos(lo+(hi-lo)*i/96),cz+rz*math.sin(lo+(hi-lo)*i/96)) for i in range(97)],r,mat,group)
def text(name,body,p,size,rot,mat='Ceramic'):
 curve=bpy.data.curves.new(name,'FONT');curve.body=body;curve.size=size;curve.align_x='CENTER';curve.extrude=.18;curve.bevel_depth=.06
 o=bpy.data.objects.new(name,curve);bpy.context.collection.objects.link(o);o.location=p;o.rotation_euler=rot
 bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o;bpy.ops.object.convert(target='MESH')
 return collect(bpy.context.object,mat,'Lettering')
def mesh(name,verts,faces,mat,group='Fittings',smooth=False):
 data=bpy.data.meshes.new(name);data.from_pydata(verts,[],faces);data.update();o=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(o)
 # Hand-built closed fittings can have inconsistent winding, especially mirrored fins.
 # Recalculate before beveling; leave intentionally open pressure skins/glazing alone.
 if group=='Fittings':
  bm=bmesh.new();bm.from_mesh(data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
 return collect(o,mat,group,smooth)

def smooth_profile(points):
 """Shape-preserving cubic profile; keep original stations and opening boundaries."""
 h=[b[0]-a[0] for a,b in zip(points,points[1:])]
 slopes=[(b[1]-a[1])/dx for a,b,dx in zip(points,points[1:],h)]
 tangents=[slopes[0]]
 for i in range(1,len(points)-1):
  a,b=slopes[i-1:i+1]
  w1=2*h[i]+h[i-1];w2=h[i]+2*h[i-1]
  tangents.append((w1+w2)/(w1/a+w2/b) if a*b>0 else 0)
 tangents.append(slopes[-1]);result=[]
 for i,((x,a),(end,b)) in enumerate(zip(points,points[1:])):
  count=max(1,math.ceil(h[i]/20))
  for j in range(count):
   t=j/count
   f=(2*t**3-3*t*t+1)*a+(t**3-2*t*t+t)*h[i]*tangents[i]+(-2*t**3+3*t*t)*b+(t**3-t*t)*h[i]*tangents[i+1]
   result.append((x+t*h[i],f))
 return result+[points[-1]]

def pressure_hull(profile,ry,rz,cz,window,entry):
 """Manifold double skin, with actual openings and connected reveal edges."""
 n=96;verts=[]
 for inward in (False,True):
  for x,f in profile:
   for j in range(n):
    t=j*2*math.pi/n
    verts.append((x,(ry*f-(10 if inward else 0))*math.cos(t),cz+(rz*f-(10 if inward else 0))*math.sin(t)))
 count=len(profile)*n;faces=[];face_mats=[];edges=defaultdict(list);glassv=[];glassf=[];glass_ids={}
 for i in range(len(profile)-1):
  xm=(profile[i][0]+profile[i+1][0])*.5
  for j in range(n):
   t=(j+.5)*2*math.pi/n;k=(j+1)%n
   a=i*n+j;b=i*n+k;c=(i+1)*n+k;d=(i+1)*n+j
   q=[a,b,c,d]
   iswindow=window(xm,t)
   if iswindow:
    ids=[]
    for idx in q:
     # Shared vertices let adjacent faces share a continuous shading normal.
     if idx not in glass_ids:
      x,y,z=verts[idx];glass_ids[idx]=len(glassv);glassv.append((x,y*.996,cz+(z-cz)*.996))
     ids.append(glass_ids[idx])
    glassf.append(ids)
   if iswindow or entry(xm,t):continue
   faces.extend([q,[a+count,d+count,c+count,b+count]])
   face_mats.extend(['Teal' if math.sin(t)<-.38 else 'Ceramic','Interior'])
   for u,v in zip(q,q[1:]+q[:1]):edges[tuple(sorted((u,v)))].append((u,v))
 for linked in edges.values():
  if len(linked)==1:
   u,v=linked[0];faces.append([v,u,u+count,v+count]);face_mats.append('Graphite')
 o=mesh('Pressure shell',verts,faces,'Ceramic','Shell',True)
 for m in ('Teal','Interior','Graphite'):o.data.materials.append(materials[m])
 indexes={'Ceramic':0,'Teal':1,'Interior':2,'Graphite':3}
 for poly,ma in zip(o.data.polygons,face_mats):poly.material_index=indexes[ma]
 mesh('Laminated observation glazing',glassv,glassf,'Glass','Glass',True)

def light_bar(name,p,length,axis='X',warm=True):
 dim=(length,12,8) if axis=='X' else (12,length,8)
 box(name+' housing',p,(dim[0]+8,dim[1]+8,12),'Graphite','Interior',3)
 box(name+' diffuser',(p[0],p[1],p[2]-7),dim,'Warm' if warm else 'Glow','Interior',2)
def monitor(name,p,width=70,height=45,tilt=0):
 box(name+' bezel',p,(width,8,height),'Graphite','Interior',4)
 box(name+' display',(p[0],p[1]-4.8,p[2]),(width-8,1,height-8),'Glow','Interior',1)
 for k in range(3):box(name+' instrument',(p[0]-width*.28+k*width*.2,p[1]-5.8,p[2]+height*.18),(width*.12,.7,2),'Ceramic','Interior',.1)
def seat(p):
 cylinder('Seat pedestal',(p[0],p[1],p[2]+28),10,56,'Titanium',group='Interior')
 box('Seat cushion',(p[0],p[1],p[2]+62),(56,58,16),'Rubber','Interior',8)
 box('Seat back',(p[0]-25,p[1],p[2]+104),(14,58,72),'Rubber','Interior',7)

# PELAGIC 01: capsule-shaped research habitat with elevated service deck and moonpool.
profile=[(-620,.06),(-600,.4),(-550,.74),(-480,.94),(-420,1),(-300,1),(-280,1),(-85,1),(85,1),(280,1),(300,1),(420,1),(480,.94),(550,.74),(600,.4),(620,.06)]
profile=smooth_profile(profile)
pressure_hull(profile,320,260,150,
 lambda x,t:abs(x)<280 and abs(math.sin(t))<.44,
 lambda x,t:abs(x)<85 and math.sin(t)<-.965)
for x in (-420,-292,292,420):ring('Pressure hoop',x,325,265,150,7,'Titanium')
for x in (-465,465):ring('Safety band',x,311,253,150,10,'Safety')
for side in (-1,1):
 for theta in ((math.pi-.458,math.pi+.458) if side==-1 else (-.458,.458)):
  pipe('Glazing perimeter',[(-282,322*math.cos(theta),150+262*math.sin(theta)),(282,322*math.cos(theta),150+262*math.sin(theta))],7,'Graphite')
 for x in (-282,0,282):
  ring('Glazing mullion',x,322,262,150,5,'Graphite',lo=(math.pi-.458 if side==-1 else -.458),hi=(math.pi+.458 if side==-1 else .458))
for x in (-400,400):
 for y in (-255,255):
  pipe('Landing strut',[(x,y,-20),(x*1.1,y*1.32,-300)],14,'Graphite')
  cylinder('Landing pad',(x*1.1,y*1.32,-318),76,28,'Titanium')
  cylinder('Hydraulic collar',(x,y,-75),23,35,'Safety')
box('Service deck',(0,-420,-90),(1120,180,22),'Graphite',radius=7)
for x in (-360,360):
 # Cantilever beams visibly enter solid lower hull, with diagonal knee braces.
 box('Deck attachment beam',(x,-320,-92),(24,355,24),'Titanium',radius=3)
 pipe('Deck knee brace',[(x,-180,-25),(x,-445,-108)],11,'Graphite')
 box('Hull mounting shoe',(x,-185,-40),(68,55,100),'Teal',radius=7)
for x in (-510,-340,340,510):pipe('Guardrail post',[(x,-493,-78),(x,-493,20)],3,'Titanium')
pipe('Deck perimeter rail',[(-535,-493,20),(535,-493,20)],4,'Safety')
for x in range(-500,501,40):box('Deck tread',(x,-420,-77),(6,146,3),'Titanium',radius=.5)
for y in (-74,74):
 pipe('Moonpool ladder rail',[(-62,y,-138),(-62,y,8)],3,'Safety','Interior')
for z in (-125,-90,-55,-20):pipe('Ladder rung',[(-62,-74,z),(-62,74,z)],3,'Titanium','Interior')
for y in (-85,85):
 cylinder('Roof reserve tank',(0,y,495),38,440,'Teal',axis='X')
 for x in (-165,165):
  cylinder('Tank retaining band',(x,y,495),41,14,'Titanium',axis='X')
  box('Tank rack saddle',(x,y,440),(38,86,38),'Graphite',radius=5)
 cylinder('Tank valve',(235,y,495),12,30,'Safety',axis='X')
pipe('Life support manifold',[(258,-85,495),(258,85,495),(285,85,410)],7,'Titanium')
box('Roof service fairing',(0,0,416),(440,100,24),'Teal',radius=12)
for x in (-170,170):
 cylinder('Sensor mast',(x,0,470),8,90,'Titanium')
 sphere('Acoustic dome',(x,0,530),(36,36,27),'Ceramic')
# Interior fitted laboratory, lockers, seats, structural ribs and safety edging.
for x in (-360,360):ring('Inner bulkhead rib',x,300,240,150,4,'Titanium','Interior',lo=.04,hi=math.pi-.04)
for x in (-270,0,270):
 box('Lab cabinet',(x,190,46),(180,60,92),'Ceramic','Interior',7)
 box('Work surface',(x,180,100),(192,82,10),'Graphite','Interior',4)
 monitor('Lab terminal',(x,198,153),78,48)
 for xx in (x-50,x+50):box('Cabinet pull',(xx,158,65),(34,4,4),'Titanium','Interior',1)
for x in (-365,365):
 box('Supply locker',(x,90,105),(58,82,210),'Teal','Interior',8)
 box('Locker handle',(x,45,112),(6,6,48),'Safety','Interior',2)
for x in (-250,250):light_bar('Ceiling luminaire',(x,0,345),155)
for x in (-150,150):seat((x,100,0))
for y in (-64,64):box('Moonpool edge',(0,y,3),(128,7,5),'Safety','Interior',1)
for x in (-64,64):box('Moonpool edge',(x,0,3),(7,128,5),'Safety','Interior',1)
for x in (-360,360):
 cylinder('End pressure hatch',(x*1.66,0,150),87,14,'Teal',axis='X')
 ring('End hatch seal',x*1.68,80,80,150,5,'Safety')
text('Habitat identification','PELAGIC  /  01',(0,-269,304),41,(math.pi/2,0,0),'Graphite')
text('Habitat designation','DEEP WATER RESEARCH',(0,-280,281),15,(math.pi/2,0,0),'Graphite')

# NEREID: streamlined expedition submarine with an enclosed, walkable pressure hull.
vessel='Submarine'
profile=[(-2070,.08),(-2030,.35),(-1960,.64),(-1880,.85),(-1750,1),(-1630,1),(-1450,1),(-1400,1),(-1000,1),(-650,1),(-300,1),(-80,.97),(60,.83),(160,.59),(220,.3),(245,.06)]
profile=smooth_profile(profile)
pressure_hull(profile,285,270,0,
 lambda x,t:x>-80 and math.sin(t)>-.32,
 lambda x,t:-1630<x<-1450 and math.sin(t)<-.95)
for x in (-1750,-1400,-1000,-650,-300):ring('Hull frame',x,289,274,0,5,'Titanium')
for x in (-1700,-285):ring('Hull safety collar',x,293,278,0,9,'Safety')
# Curved bridge edge and swept canopy ribs.
ring('Bridge aperture frame',-80,279,264,0,7,'Graphite',lo=-.327,hi=math.pi+.327)
for theta in (-.327,0,math.pi/2,math.pi,math.pi+.327):
 pipe('Bridge canopy rib',[(x,(285*f+3)*math.cos(theta),(270*f+3)*math.sin(theta)) for x,f in profile if x>=-80],5,'Graphite')
# Rounded sail, periscope and communications mast.
verts=[(-1230,-64,225),(-720,-64,225),(-860,-64,515),(-1110,-64,515),(-1230,64,225),(-720,64,225),(-860,64,515),(-1110,64,515)]
o=mesh('Hydrodynamic sail',verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'Teal');bevel(o,22,5)
for x,h in ((-1030,700),(-880,620)):
 cylinder('Periscope',(x,0,(500+h)/2),10,h-500,'Titanium')
 sphere('Sensor head',(x+18,0,h),(34,22,18),'Graphite')
box('Sail crown',(-985,0,510),(240,125,18),'Ceramic',radius=9)
# Tailplane profiles (tapered hydrofoils, no cube silhouette).
def foil(name,verts):
 o=mesh(name,verts,[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],'Teal');bevel(o,5,3)
for side in (-1,1):
 foil('Horizontal stabilizer',[(-1990,side*100,-16),(-1810,side*100,-16),(-1930,side*520,-8),(-2060,side*520,-8),(-1990,side*100,16),(-1810,side*100,16),(-1930,side*520,8),(-2060,side*520,8)])
 foil('Vertical stabilizer',[(-2000,-12,side*110),(-1800,-12,side*110),(-1930,-6,side*440),(-2050,-6,side*440),(-2000,12,side*110),(-1800,12,side*110),(-1930,6,side*440),(-2050,6,side*440)])
for side in (-1,1):
 foil('Bow dive plane',[(-440,side*245,-80),(-180,side*245,-80),(-250,side*470,-80),(-410,side*470,-80),(-440,side*245,-66),(-180,side*245,-66),(-250,side*470,-74),(-410,side*470,-74)])
# Open annular propulsor duct and static five-blade propeller.
for x in (-2280,-2170):ring('Propulsor duct rim',x,153,153,0,15,'Titanium')
v=[];f=[]
for x,r in ((-2280,166),(-2170,166),(-2170,141),(-2280,141)):
 for j in range(64):v.append((x,r*math.cos(j*math.tau/64),r*math.sin(j*math.tau/64)))
for k in range(4):
 for j in range(64):f.append((k*64+j,k*64+(j+1)%64,((k+1)%4)*64+(j+1)%64,((k+1)%4)*64+j))
mesh('Propulsor shroud',v,f,'Teal',smooth=True)
cylinder('Propeller shaft',(-2150,0,0),22,250,'Titanium','X')
sphere('Propeller hub',(-2225,0,0),(44,35,35),'Safety')
for j in range(5):
 t=j*math.tau/5
 o=box('Swept propeller blade',(-2225,85*math.cos(t),85*math.sin(t)),(13,100,35),'Titanium',radius=9)
 o.rotation_euler=(t,math.radians(24),0)
for y in (-205,205):
 pipe('Floodlight mounting arm',[(40,math.copysign(190,y),-130),(135,y,-135),(170,y,-135)],12,'Titanium')
 cylinder('Forward floodlight housing',(190,y,-135),35,60,'Graphite','X')
 cylinder('Floodlight lens',(222,y,-135),27,3,'Warm','X')
for y in (-242,242):
 pipe('Longitudinal rub rail',[(-1700,y,-130),(-300,y,-130)],11,'Graphite')
 for x in (-1550,-1150,-750):
  cylinder('Service port',(x,y*1.16,30),27,8,'Titanium','Y')
  cylinder('Service port core',(x,y*1.18,30),18,9,'Graphite','Y')
text('Submarine name','N E R E I D',(-1010,-286,30),52,(math.pi/2,0,0),'Ceramic')
text('Submarine registry','EXPEDITION  /  07',(-1010,-282,-15),18,(math.pi/2,0,0),'Safety')
text('Submarine starboard name','N E R E I D',(-1010,286,30),52,(math.pi/2,0,math.pi),'Ceramic')
# Internal pressure frames leave the centre aisle open.
for x in (-1320,-850,-380):ring('Interior transverse frame',x,266,250,0,5,'Titanium','Interior',lo=-.62,hi=math.pi+.62)
for y in (-162,162):
 for x in (-1240,-910,-580):
  box('Equipment cabinet',(x,y,-95),(190,58,150),'Ceramic','Interior',10)
  box('Instrument face',(x,y-math.copysign(33,y),-65),(164,5,85),'Graphite','Interior',4)
  for dx in (-55,0,55):box('Instrument status',(x+dx,y-math.copysign(37,y),-52),(26,3,6),'Glow','Interior',1)
for x in (-1270,-850,-440):light_bar('Aisle light',(x,0,185),155)
# Helm console: oriented toward +X, with view through the glazed nose.
box('Bridge console',(-20,0,-109),(115,285,95),'Teal','Interior',14)
for y in (-87,0,87):
 box('Helm screen surround',(-72,y,-42),(10,78,48),'Graphite','Interior',5)
 box('Helm active screen',(-78,y,-42),(2,67,36),'Glow','Interior',2)
 for z in (-48,-37):box('Helm readout',(-79.3,y,z),(1,45,2),'Ceramic','Interior',.2)
seat((-210,0,-170))
for y in (-85,85):pipe('Hatch ladder rail',[(-1606,y,-280),(-1606,y,-154)],3,'Safety','Interior')
for z in (-260,-225,-190):pipe('Hatch ladder rung',[(-1606,-85,z),(-1606,85,z)],3,'Titanium','Interior')
for y in (-83,83):box('Entry rim',(-1540,y,-167),(170,7,5),'Safety','Interior',1)
for x in (-1623,-1457):box('Entry rim',(x,0,-167),(7,170,5),'Safety','Interior',1)

# Export one mesh per semantic group, all sharing the original actor-space origin.
manifest=[]
for group,objects in groups.items():
 bpy.ops.object.select_all(action='DESELECT')
 for o in objects:o.select_set(True)
 bpy.context.view_layer.objects.active=objects[0]
 bpy.ops.object.join();o=bpy.context.object;o.name='SM_'+group
 bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
 bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
 # Explicit triangulation makes FBX topology deterministic.
 mod=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
 target=os.path.join(OUT,o.name+'.fbx')
 bpy.ops.export_scene.fbx(filepath=target,use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,add_leaf_bones=False,mesh_smooth_type='FACE',use_mesh_modifiers=True)
 mins=[min((o.matrix_world@Vector(c))[k] for c in o.bound_box) for k in range(3)]
 maxs=[max((o.matrix_world@Vector(c))[k] for c in o.bound_box) for k in range(3)]
 manifest.append({'mesh':o.name,'group':group,'file':target,'bounds':[mins,maxs],'triangles':len(o.data.polygons),'materials':[m.name for m in o.data.materials]})
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT,'PressureVessels.blend'))
with open(os.path.join(OUT,'manifest.json'),'w') as f:json.dump({'materials':palette,'meshes':manifest},f,indent=2)
print('VESSELS_EXPORTED',json.dumps(manifest))
