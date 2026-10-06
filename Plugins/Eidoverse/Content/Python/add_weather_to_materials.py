"""Weather on surfaces (client/lib/materials.js): a wetness wrap and puddles inserted
into the base colour and roughness of the world's PBR masters, driven by
/Eidoverse/FX/MPC_EidoverseWeather (Wet 0..1). Idempotent (skips materials that
already carry the EidoWeather node). Also builds M_EidoverseCloudShadow, a light
function for the sun (Coverage, Strength, Scroll1, Scroll2 from the same MPC).
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>"""
import traceback
import unreal
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
TARGETS = ['/glTFRuntime/M_glTFRuntimeBase', '/glTFRuntime/M_ClearCoat_glTFRuntimeBase', '/glTFRuntime/M_glTFRuntime_SG_Base',
           '/Game/Underwater/Avatars/Materials/M_EidoverseSurface', '/Game/Underwater/Avatars/Materials/M_EidoverseTerrain',
           '/Game/Underwater/Avatars/Materials/M_EidoverseGroundCover', '/Game/Underwater/Avatars/Materials/M_EidoverseFlora']
HLSL = '''
// materials.js: up-facing surfaces darken and gloss when wet; flat ground pools.
float up = saturate(Nz); float upMask = up * up * Wet;
float3 c = Base * lerp(1, 0.68, upMask * 0.85);
float r = lerp(Rough, 0.10, upMask * 0.8);
float pShape = smoothstep(0.97, 1.13, Pn) * smoothstep(0.985, 0.998, Nz);
float puddle = smoothstep(0.25, 0.6, pShape) * smoothstep(0.35, 0.9, Wet) * Puddles;
r = lerp(r, 0.045, puddle);
c = lerp(c, c * lerp(0.96, 0.62, puddle) * float3(0.84, 0.91, 1.0), puddle);
return float4(c, r);
'''
report = []
def mpc():
    p = unreal.load_asset('/Eidoverse/FX/MPC_EidoverseWeather')
    if not p:
        p = TOOLS.create_asset('MPC_EidoverseWeather', '/Eidoverse/FX', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    names = [s.get_editor_property('parameter_name') for s in p.get_editor_property('scalar_parameters')]
    scalars = list(p.get_editor_property('scalar_parameters'))
    for n, d in (('Wet', 0.0), ('Coverage', 0.0), ('Strength', 0.0), ('Scroll1X', 0.0), ('Scroll1Y', 0.0), ('Scroll2X', 0.0), ('Scroll2Y', 0.0)):
        if n not in [str(x) for x in names]:
            s = unreal.CollectionScalarParameter(); s.set_editor_property('parameter_name', n); s.set_editor_property('default_value', d); scalars.append(s)
    p.set_editor_property('scalar_parameters', scalars)
    unreal.EditorAssetLibrary.save_loaded_asset(p)
    return p
def collection_param(mat, coll, name, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter, x, y)
    n.set_editor_property('collection', coll); n.set_editor_property('parameter_name', name)
    return n
def wrap(path, coll):
    mat = unreal.load_asset(path)
    if not mat: report.append(path + ': missing'); return
    for e in MEL.get_material_expressions(mat) if hasattr(MEL, 'get_material_expressions') else []:
        if isinstance(e, unreal.MaterialExpressionCustom) and e.get_editor_property('description') == 'EidoWeather':
            report.append(path + ': already'); return
    base = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    base_out = MEL.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_BASE_COLOR) if base else ''
    rough = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_ROUGHNESS)
    rough_out = MEL.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_ROUGHNESS) if rough else ''
    if not base: report.append(path + ': no base colour input'); return
    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, 400, -600)
    custom.set_editor_property('description', 'EidoWeather')
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('code', HLSL)
    ins = []
    for n in ('Base', 'Rough', 'Nz', 'Wet', 'Pn', 'Puddles'):
        i = unreal.CustomInput(); i.set_editor_property('input_name', n); ins.append(i)
    custom.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(base, base_out, custom, 'Base')
    if rough: MEL.connect_material_expressions(rough, rough_out, custom, 'Rough')
    else:
        k = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, 200, -500); k.set_editor_property('r', 0.5)
        MEL.connect_material_expressions(k, '', custom, 'Rough')
    nrm = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexNormalWS, 0, -450)
    nz = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 200, -450)
    nz.set_editor_property('r', False); nz.set_editor_property('g', False); nz.set_editor_property('b', True); nz.set_editor_property('a', False)
    MEL.connect_material_expressions(nrm, '', nz, ''); MEL.connect_material_expressions(nz, '', custom, 'Nz')
    MEL.connect_material_expressions(collection_param(mat, coll, 'Wet', 200, -380), '', custom, 'Wet')
    # Pn = vnoise(xz * .35) + .6 vnoise(xz * .09) over world metres
    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -400, -300)
    m = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, -300); m.set_editor_property('const_b', 0.01)
    MEL.connect_material_expressions(wp, '', m, 'A')
    def noise(scale, x, y):
        s = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, x - 150, y); s.set_editor_property('const_b', scale)
        MEL.connect_material_expressions(m, '', s, 'A')
        nn = MEL.create_material_expression(mat, unreal.MaterialExpressionNoise, x, y)
        nn.set_editor_property('output_min', 0.0); nn.set_editor_property('output_max', 1.0); nn.set_editor_property('levels', 2)
        nn.set_editor_property('turbulence', False)
        MEL.connect_material_expressions(s, '', nn, 'Position')
        return nn
    n1 = noise(0.35, 0, -300); n2 = noise(0.09, 0, -200)
    w2 = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, 150, -200); w2.set_editor_property('const_b', 0.6)
    MEL.connect_material_expressions(n2, '', w2, 'A')
    add = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, 250, -260)
    MEL.connect_material_expressions(n1, '', add, 'A'); MEL.connect_material_expressions(w2, '', add, 'B')
    MEL.connect_material_expressions(add, '', custom, 'Pn')
    pud = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, 250, -150)
    pud.set_editor_property('r', 0.0 if ('GroundCover' in path or 'Flora' in path) else 1.0)   # puddles: not on grass
    MEL.connect_material_expressions(pud, '', custom, 'Puddles')
    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 650, -620)
    rgb.set_editor_property('r', True); rgb.set_editor_property('g', True); rgb.set_editor_property('b', True); rgb.set_editor_property('a', False)
    rr = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 650, -520)
    rr.set_editor_property('r', False); rr.set_editor_property('g', False); rr.set_editor_property('b', False); rr.set_editor_property('a', True)
    MEL.connect_material_expressions(custom, '', rgb, ''); MEL.connect_material_expressions(custom, '', rr, '')
    MEL.connect_material_property(rgb, '', unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rr, '', unreal.MaterialProperty.MP_ROUGHNESS)
    errs = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    report.append('%s: wrapped (rough=%s) %s' % (path, bool(rough), list(errs) if errs else []))
def cloud_shadow(coll):
    name, path = 'M_EidoverseCloudShadow', '/Eidoverse/FX'
    mat = unreal.load_asset(path + '/' + name)
    if not mat: mat = TOOLS.create_asset(name, path, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -200, 0)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property('code', '''
// materials.js cloud shade: two scrolling noise octaves, lo = 1.02 - .72 cover.
float lo = 1.02 - 0.72 * Cover;
float d = 0.72 * N1 + 0.28 * N2;
return 1 - smoothstep(lo, lo + 0.22, d) * Strength;''')
    ins = []
    for n in ('Cover', 'Strength', 'N1', 'N2'):
        i = unreal.CustomInput(); i.set_editor_property('input_name', n); ins.append(i)
    custom.set_editor_property('inputs', ins)
    MEL.connect_material_expressions(collection_param(mat, coll, 'Coverage', -500, -100), '', custom, 'Cover')
    MEL.connect_material_expressions(collection_param(mat, coll, 'Strength', -500, -40), '', custom, 'Strength')
    wp = MEL.create_material_expression(mat, unreal.MaterialExpressionWorldPosition, -1100, 100)
    def octave(scale, sx, sy, y):
        mm = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -900, y); mm.set_editor_property('const_b', 0.01 / scale)
        MEL.connect_material_expressions(wp, '', mm, 'A')
        off = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -900, y + 60)
        a = collection_param(mat, coll, sx, -1100, y + 60); b = collection_param(mat, coll, sy, -1100, y + 120)
        MEL.connect_material_expressions(a, '', off, 'A'); MEL.connect_material_expressions(b, '', off, 'B')
        off3 = MEL.create_material_expression(mat, unreal.MaterialExpressionAppendVector, -800, y + 60)
        z = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -900, y + 150); z.set_editor_property('r', 0.0)
        MEL.connect_material_expressions(off, '', off3, 'A'); MEL.connect_material_expressions(z, '', off3, 'B')
        ad = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, -700, y)
        MEL.connect_material_expressions(mm, '', ad, 'A'); MEL.connect_material_expressions(off3, '', ad, 'B')
        nn = MEL.create_material_expression(mat, unreal.MaterialExpressionNoise, -550, y)
        nn.set_editor_property('output_min', 0.0); nn.set_editor_property('output_max', 1.0); nn.set_editor_property('levels', 3)
        MEL.connect_material_expressions(ad, '', nn, 'Position')
        return nn
    MEL.connect_material_expressions(octave(420, 'Scroll1X', 'Scroll1Y', 100), '', custom, 'N1')
    MEL.connect_material_expressions(octave(97, 'Scroll2X', 'Scroll2Y', 350), '', custom, 'N2')
    MEL.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    errs = MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    report.append('cloud shadow: %s' % (list(errs) if errs else []))
try:
    coll = mpc()
    for t in TARGETS: wrap(t, coll)
    cloud_shadow(coll)
    unreal.log_warning('WEATHER_MATERIALS ' + ' | '.join(report))
except Exception:
    unreal.log_error('WEATHER_MATERIALS_FAILED\n' + traceback.format_exc() + ' | '.join(report))
finally:
    unreal.SystemLibrary.quit_editor()
