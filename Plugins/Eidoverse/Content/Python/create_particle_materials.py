"""Create /Eidoverse/FX/M_EidoverseParticleAdd and M_EidoverseParticleAlpha: unlit
translucent sprite materials for particles.js-style emitters. Per-instance custom
data: 0 = alpha, 1..3 = colour. Texture param Sprite with UseSprite (0 = soft dot).
Idempotent.  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>"""
import traceback
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
MEL = unreal.MaterialEditingLibrary
PATH = '/Eidoverse/FX'
def build(name, additive):
    mat = unreal.load_asset(PATH + '/' + name)
    if not mat:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, PATH, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    mat.set_editor_property('used_with_instanced_static_meshes', True)
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -900, 0)
    alpha.set_editor_property('data_index', 0)
    cols = []
    for i in range(3):
        c = MEL.create_material_expression(mat, unreal.MaterialExpressionPerInstanceCustomData, -900, 120 + i * 100)
        c.set_editor_property('data_index', 1 + i)
        cols.append(c)
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -700, 160)
    rg = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -800, 140)
    MEL.connect_material_expressions(cols[0], '', rg, 'A'); MEL.connect_material_expressions(cols[1], '', rg, 'B')
    MEL.connect_material_expressions(rg, '', rgb, 'A'); MEL.connect_material_expressions(cols[2], '', rgb, 'B')
    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 500)
    tex.set_editor_property('parameter_name', 'Sprite')
    use = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, 750)
    use.set_editor_property('parameter_name', 'UseSprite'); use.set_editor_property('default_value', 0.0)
    uvs = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -1100, 850)
    # soft dot: smoothstep(.5, .12, |uv-.5|); rim fade for sprites: smoothstep(.5,.44,max|uv-.5|)
    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -600, 600)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('code',
        'float2 d = abs(UV - 0.5);\n'
        'float dot1 = 1 - smoothstep(0.12, 0.5, length(UV - 0.5));\n'
        'float rim = 1 - smoothstep(0.44, 0.5, max(d.x, d.y));\n'
        'float3 c = lerp(float3(1,1,1), Tex.rgb, Use);\n'
        'float a = lerp(dot1, Tex.a * rim, Use);\n'
        'return float4(c * Col, max(0, a * Alpha - 0.004));')
    ins = []
    for n in ('UV', 'Tex', 'Use', 'Col', 'Alpha'):
        i = unreal.CustomInput(); i.set_editor_property('input_name', n); ins.append(i)
    custom.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(uvs, '', custom, 'UV')
    MEL.connect_material_expressions(tex, 'RGBA', custom, 'Tex')
    MEL.connect_material_expressions(use, '', custom, 'Use')
    MEL.connect_material_expressions(rgb, '', custom, 'Col')
    MEL.connect_material_expressions(alpha, '', custom, 'Alpha')
    mask_rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -350, 560)
    mask_rgb.set_editor_property('r', True); mask_rgb.set_editor_property('g', True); mask_rgb.set_editor_property('b', True); mask_rgb.set_editor_property('a', False)
    mask_a = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -350, 700)
    mask_a.set_editor_property('r', False); mask_a.set_editor_property('g', False); mask_a.set_editor_property('b', False); mask_a.set_editor_property('a', True)
    MEL.connect_material_expressions(custom, '', mask_rgb, '')
    MEL.connect_material_expressions(custom, '', mask_a, '')
    if additive:
        prem = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 560)
        MEL.connect_material_expressions(mask_rgb, '', prem, 'A'); MEL.connect_material_expressions(mask_a, '', prem, 'B')
        MEL.connect_material_property(prem, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        MEL.connect_material_property(mask_rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(mask_a, '', unreal.MaterialProperty.MP_OPACITY)
    errors = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return list(errors) if errors else []
try:
    e1 = build('M_EidoverseParticleAdd', True)
    e2 = build('M_EidoverseParticleAlpha', False)
    unreal.log_warning('PARTICLE_MATERIALS_READY %s %s' % (e1, e2))
except Exception:
    unreal.log_error('PARTICLE_MATERIALS_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
