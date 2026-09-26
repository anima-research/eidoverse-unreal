"""Camera-centred graded ocean grid; run with Blender in background mode."""
import bpy,os
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.context.scene.unit_settings.system='METRIC';bpy.context.scene.unit_settings.scale_length=.01
positive=[float(x) for x in range(0,6001,50)]
x=6000.;step=50.
while x<1000000:
    step*=1.065;x=min(1000000,x+step);positive.append(x)
axis=[-v for v in reversed(positive[1:])]+positive;n=len(axis)
verts=[(x,y,0) for y in axis for x in axis]
faces=[(j*n+i,j*n+i+1,(j+1)*n+i+1,(j+1)*n+i) for j in range(n-1) for i in range(n-1)]
mesh=bpy.data.meshes.new('Spectral camera grid');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('SM_SpectralOceanGrid',mesh);bpy.context.collection.objects.link(obj)
obj.select_set(True);bpy.context.view_layer.objects.active=obj
for p in mesh.polygons:p.use_smooth=True
out=os.path.join(os.path.dirname(__file__),'exports');os.makedirs(out,exist_ok=True)
bpy.ops.export_scene.fbx(filepath=os.path.join(out,'SM_SpectralOceanGrid.fbx'),use_selection=True,axis_forward='-Y',axis_up='Z',apply_unit_scale=True,mesh_smooth_type='FACE')
print('SPECTRAL_GRID',n,len(verts),2*len(faces))
