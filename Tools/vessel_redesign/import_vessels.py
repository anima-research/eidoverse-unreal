"""Import new vessel assets only. Does not replace scene or Blueprint geometry."""
import unreal, json, os
ROOT='/Game/Underwater/Vessels'
DATA=json.load(open('/Users/olena/unrealtest/vessel_redesign/exports/manifest.json'))
AT=unreal.AssetToolsHelpers.get_asset_tools(); MEL=unreal.MaterialEditingLibrary
def prop(o,n,v):o.set_editor_property(n,v)
def expr(m,c,**kw):
 e=MEL.create_material_expression(m,c)
 for k,v in kw.items():prop(e,k,v)
 return e
materials={}
for name,(rgb,metal,rough) in DATA['materials'].items():
 if name=='Glass':
  materials['V_'+name]=unreal.load_asset('/Game/Underwater/Materials/M_Glass');continue
 assetname='M_Vessel_'+name;m=unreal.load_asset(ROOT+'/Materials/'+assetname)
 if not m:
  m=AT.create_asset(assetname,ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
  base=expr(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*rgb,1))
  met=expr(m,unreal.MaterialExpressionConstant,r=metal)
  rou=expr(m,unreal.MaterialExpressionConstant,r=rough)
  MEL.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
  MEL.connect_material_property(met,'',unreal.MaterialProperty.MP_METALLIC)
  MEL.connect_material_property(rou,'',unreal.MaterialProperty.MP_ROUGHNESS)
  if name in ('Glow','Warm'):
   glow=expr(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*(v*2 for v in rgb),1))
   MEL.connect_material_property(glow,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  MEL.recompile_material(m);unreal.EditorAssetLibrary.save_loaded_asset(m)
 materials['V_'+name]=m

for item in DATA['meshes']:
 if os.environ.get('VESSEL_IMPORT_ONLY') and item['mesh']!=os.environ['VESSEL_IMPORT_ONLY']:continue
 task=unreal.AssetImportTask();prop(task,'filename',item['file']);prop(task,'destination_path',ROOT+'/Meshes')
 prop(task,'destination_name',item['mesh']);prop(task,'automated',True);prop(task,'replace_existing',True);prop(task,'save',True)
 options=unreal.FbxImportUI();prop(options,'import_mesh',True);prop(options,'import_materials',False);prop(options,'import_textures',False)
 prop(options,'import_as_skeletal',False);prop(options,'mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)
 prop(options,'automated_import_should_detect_type',False)
 sm=options.get_editor_property('static_mesh_import_data')
 for n,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('convert_scene',True),('convert_scene_unit',True),('import_uniform_scale',1.0)]:prop(sm,n,v)
 prop(sm,'normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS)
 prop(task,'options',options);prop(task,'factory',unreal.FbxFactory())
 AT.import_asset_tasks([task])
 mesh=unreal.load_asset(ROOT+'/Meshes/'+item['mesh']);assert mesh,item
 box=mesh.get_bounding_box();size=box.max-box.min
 expected=[item['bounds'][1][k]-item['bounds'][0][k] for k in range(3)]
 assert abs(size.x-expected[0])<2 and abs(size.y-expected[1])<2 and abs(size.z-expected[2])<2,(item['mesh'],size,expected)
 for index,slot in enumerate(mesh.get_editor_property('static_materials')):
  name=str(slot.get_editor_property('imported_material_slot_name'))
  assert name in materials,(item['mesh'],name)
  mesh.set_material(index,materials[name])
 body=mesh.get_editor_property('body_setup')
 if body:
  prop(body,'collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
  prop(body,'double_sided_geometry',item['group'].endswith('_Glass'))
 unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 unreal.log('VESSEL_IMPORT_OK '+item['mesh']+' bounds='+str(size))
unreal.log('VESSEL_IMPORT_SUCCESS')
