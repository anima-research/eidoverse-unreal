"""Graded ocean mesh: fine near the playable area, coarse out to the horizon."""
import bpy,math,os
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=.01
positive=[float(i) for i in range(0,18001,100)]
x=18000.;step=100.
while x<1000000:
 step*=1.07;x=min(x+step,1000000);positive.append(x)
axis=[-x for x in reversed(positive[1:])]+positive
n=len(axis);verts=[(x,y,0) for y in axis for x in axis]
faces=[(j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i) for j in range(n-1) for i in range(n-1)]
mesh=bpy.data.meshes.new('Ocean grid');mesh.from_pydata(verts,[],faces);mesh.update()
o=bpy.data.objects.new('SM_OceanSurface',mesh);bpy.context.collection.objects.link(o)
o.select_set(True);bpy.context.view_layer.objects.active=o
for p in mesh.polygons:p.use_smooth=True
out=os.path.join(os.path.dirname(__file__),'exports','SM_OceanSurface.fbx')
bpy.ops.export_scene.fbx(filepath=out,use_selection=True,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,mesh_smooth_type='FACE')
print('OCEAN_GRID',n,len(verts),len(faces)*2,out)
