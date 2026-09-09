"""Import the original ecology kit and author world-space, non-tiled materials."""
import unreal,json,os
from editor_toolset.toolsets.material import MaterialTools as M
ROOT='/Game/Underwater/Ecology';MEL=unreal.MaterialEditingLibrary;AT=unreal.AssetToolsHelpers.get_asset_tools()
DATA=json.load(open('/Users/olena/unrealtest/marine_ecology/exports/manifest.json'))
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
def prop(o,n,v):o.set_editor_property(n,v)
def expr(mat,cls,**kw):
 o=MEL.create_material_expression(mat,cls)
 for n,v in kw.items():prop(o,n,v)
 return o
def custom(mat,code,inputs,output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
 o=expr(mat,unreal.MaterialExpressionCustom,code=code,output_type=output)
 pins=[]
 for name in inputs:
  p=unreal.CustomInput();prop(p,'input_name',name);pins.append(p)
 prop(o,'inputs',pins)
 for name,node in inputs.items():assert MEL.connect_material_expressions(node,'',o,name)
 return o
noise='''struct N {float h(float3 p){return frac(sin(dot(p,float3(127.1,311.7,74.7)))*43758.5453);} float n(float3 p){float3 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(h(i),h(i+float3(1,0,0)),f.x),lerp(h(i+float3(0,1,0)),h(i+float3(1,1,0)),f.x),f.y),lerp(lerp(h(i+float3(0,0,1)),h(i+float3(1,0,1)),f.x),lerp(h(i+float3(0,1,1)),h(i+1),f.x),f.y),f.z);}};N n;\n'''
materials={}
for name,(rgb,metal,rough) in DATA['palette'].items():
 mat=unreal.load_asset(ROOT+'/Materials/M_'+name)
 if mat and os.environ.get('ECOLOGY_IMPORT_ONLY'):
  materials['E_'+name]=mat;continue
 if not mat:mat=AT.create_asset('M_'+name,ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
 else:
  for e in M.get_expressions(mat):MEL.delete_material_expression(mat,e)
 prop(mat,'two_sided',name in ('Kelp','RedAlgae','Grass','Fin','Ray','Star'))
 pos=expr(mat,unreal.MaterialExpressionWorldPosition)
 base=expr(mat,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*rgb,1))
 if name in ('Sand','Rock','Kelp','RedAlgae','Grass','Rockfish','Ray'):
  code=noise+'float f=n.n(P*0.017)*0.55+n.n(P*0.063)*0.3+n.n(P*0.27)*0.15; float3 color=B*(0.65+f*.7);\n'
  if name=='Sand':code+='float r=sin(dot(P.xy,float2(.108,.047))+1.7*n.n(P*.0017));color*=0.97+0.03*r;\n'
  if name=='Rock':code+='float beds=pow(saturate(sin(P.z*.085+2*n.n(P*.006))),12);color*=1-beds*.3;float crust=smoothstep(.62,.76,n.n(P*.008));color=lerp(color,float3(.22,.11,.13),crust*.28);\n'
  if name=='Ray':code+='color*=1-.25*smoothstep(.68,.78,n.n(P*.28));\n'
  base=custom(mat,code+'return color;',{'P':pos,'B':base})
 MEL.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
 for p,v in [(unreal.MaterialProperty.MP_METALLIC,metal),(unreal.MaterialProperty.MP_ROUGHNESS,rough)]:MEL.connect_material_property(expr(mat,unreal.MaterialExpressionConstant,r=v),'',p)
 if name in ('Sand','Rock'):
  normal=custom(mat,'float r=dot(P.xy,float2(.108,.047))+1.7*sin(P.x*.0017);return normalize(float3(cos(r)*.05,sin(P.y*.11)*.025,1));' if name=='Sand' else 'return normalize(float3(sin(P.x*.08+P.z*.031)*.17,cos(P.y*.067+P.z*.04)*.16,1));',{'P':pos})
  MEL.connect_material_property(normal,'',unreal.MaterialProperty.MP_NORMAL)
 if name in ('Kelp','RedAlgae','Grass','Silver','Back','Fin','Rockfish','Ray'):
  color=expr(mat,unreal.MaterialExpressionVertexColor);time=expr(mat,unreal.MaterialExpressionTime)
  # VertexColor output '' is RGB; request Alpha separately for individual fish phase.
  if name in ('Kelp','RedAlgae','Grass'):
   radius=expr(mat,unreal.MaterialExpressionObjectRadius)
   wpo=custom(mat,'float ph=P.x*.0007+P.y*.0011;float a=min(45.0,Radius*.035)*C.r;return float3(sin(T*.55+ph)*a,sin(T*.43+ph+1.2)*a*.6,0);',{'P':pos,'T':time,'C':color,'Radius':radius})
  elif name=='Ray':wpo=custom(mat,'return float3(0,0,sin(T*1.3)*C.r*5);',{'T':time,'C':color})
  else:
   wpo=custom(mat,'return float3(C.b*2-1,C.g*2-1,0)*sin(T*7.5+P.z*.11)*C.r*1.7;',{'T':time,'C':color,'P':pos})
  # Convert local biological bend directions with the rotating school actor.
  if name in ('Silver','Back','Fin','Rockfish'):
   tr=expr(mat,unreal.MaterialExpressionTransform,transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL,transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
   assert MEL.connect_material_expressions(wpo,'',tr,'');wpo=tr
  MEL.connect_material_property(wpo,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
 MEL.layout_material_expressions(mat);MEL.recompile_material(mat);unreal.EditorAssetLibrary.save_loaded_asset(mat)
 materials['E_'+name]=mat
for item in DATA['meshes']:
 if os.environ.get('ECOLOGY_IMPORT_ONLY') and item['name']!=os.environ['ECOLOGY_IMPORT_ONLY']:continue
 task=unreal.AssetImportTask()
 for n,v in [('filename',item['file']),('destination_path',ROOT+'/Meshes'),('destination_name',item['mesh']),('automated',True),('replace_existing',True),('save',True)]:prop(task,n,v)
 options=unreal.FbxImportUI()
 for n,v in [('import_mesh',True),('import_materials',False),('import_textures',False),('import_as_skeletal',False),('automated_import_should_detect_type',False),('mesh_type_to_import',unreal.FBXImportType.FBXIT_STATIC_MESH)]:prop(options,n,v)
 sm=options.get_editor_property('static_mesh_import_data')
 for n,v in [('combine_meshes',True),('auto_generate_collision',False),('generate_lightmap_u_vs',False),('convert_scene',True),('convert_scene_unit',True),('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS),('vertex_color_import_option',unreal.VertexColorImportOption.REPLACE)]:prop(sm,n,v)
 prop(task,'options',options);prop(task,'factory',unreal.FbxFactory());AT.import_asset_tasks([task])
 mesh=unreal.load_asset(ROOT+'/Meshes/'+item['mesh']);assert mesh
 for i,slot in enumerate(mesh.get_editor_property('static_materials')):mesh.set_material(i,materials[str(slot.get_editor_property('imported_material_slot_name'))])
 body=mesh.get_editor_property('body_setup')
 if body:prop(body,'collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
 if item['name']!='ShelfTerrain':
  prop(mesh,'positive_bounds_extension',unreal.Vector(65,65,12));prop(mesh,'negative_bounds_extension',unreal.Vector(65,65,12))
 unreal.EditorAssetLibrary.save_loaded_asset(mesh)
 unreal.log('ECOLOGY_IMPORT_OK '+item['name'])
unreal.log('ECOLOGY_IMPORT_SUCCESS')
