"""Dense instanced grass: opaque blades, vertex tint/wind and geometric distance fade.
Editor only; does not modify the general flora or any production world.
"""
import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
lib=unreal.MaterialEditingLibrary
path='/Game/Underwater/Avatars/Materials/M_EidoverseGroundCover'
m=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_EidoverseGroundCover','/Game/Underwater/Avatars/Materials',unreal.Material,unreal.MaterialFactoryNew())
lib.delete_all_material_expressions(m)
m.set_editor_property('two_sided',True)
m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
lib.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
def node(cls):return lib.create_material_expression(m,cls)
def custom(kind,code,inputs):
    c=node(unreal.MaterialExpressionCustom);c.set_editor_property('output_type',kind);c.set_editor_property('code',code)
    slots=[]
    for name,n,out in inputs:
        i=unreal.CustomInput();i.set_editor_property('input_name',name);slots.append(i)
    c.set_editor_property('inputs',slots)
    for name,n,out in inputs:assert lib.connect_material_expressions(n,out,c,name)
    return c
data=[]
for index,default in enumerate([.08,.22,.035,1,50]):
    c=node(unreal.MaterialExpressionPerInstanceCustomData)
    c.set_editor_property('data_index',index);c.set_editor_property('const_default_value',default);data.append(c)
vc=node(unreal.MaterialExpressionVertexColor)
tint=custom(unreal.CustomMaterialOutputType.CMOT_FLOAT3,'return V.rgb*float3(R,G,B);',
            [('V',vc,''),('R',data[0],''),('G',data[1],''),('B',data[2],'')])
interpolator=node(unreal.MaterialExpressionVertexInterpolator)
assert lib.connect_material_expressions(tint,'',interpolator,'')
assert lib.connect_material_property(interpolator,'',unreal.MaterialProperty.MP_BASE_COLOR)
sub=custom(unreal.CustomMaterialOutputType.CMOT_FLOAT3,'return C*.55;', [('C',interpolator,'')])
assert lib.connect_material_property(sub,'',unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
rough=node(unreal.MaterialExpressionConstant);rough.set_editor_property('r',.9)
assert lib.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
spec=node(unreal.MaterialExpressionConstant);spec.set_editor_property('r',.2)
assert lib.connect_material_property(spec,'',unreal.MaterialProperty.MP_SPECULAR)
pos=node(unreal.MaterialExpressionWorldPosition);time=node(unreal.MaterialExpressionTime)
uv=node(unreal.MaterialExpressionTextureCoordinate);camera=node(unreal.MaterialExpressionCameraPositionWS)
wind=custom(unreal.CustomMaterialOutputType.CMOT_FLOAT3,
'''float tip=saturate(UV.y);
float fade=saturate((4500-distance(P,Camera))/1700);
float breeze=sin(P.x*.004+P.y*.007+T*1.2)+.3*sin(P.y*.018-T*2.4);
return float3(breeze*3.2,breeze*1.8,0)*tip*tip*Wind*fade-float3(0,0,tip*Height*(1-fade));''',
 [('P',pos,''),('T',time,''),('UV',uv,''),('Camera',camera,''),('Wind',data[3],''),('Height',data[4],'')])
assert lib.connect_material_property(wind,'',unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
lib.recompile_material(m)
assert unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log('EIDOVERSE_GROUND_COVER_MATERIAL_READY')
