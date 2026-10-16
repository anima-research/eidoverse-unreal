"""M_EidoverseLevelWater + MPC_EidoverseWater: the world's one water level (client/lib/water.js).
Vertex data from the native mesh: color R depth/10, G shore/20, B kind/2 (0 still, .5 river, 1 ocean);
UV1 = river flow or ocean way-to-shore (wire x, z); UV2.x = river wave speed. MPC: WaveT (s on the
shared clock), OriginX/OriginY (UE cm of the scene origin). Waves are shared/waves.js surfaceOffset,
in a Custom node, as WPO and as an analytic normal. Params: Color, Opacity. Idempotent."""
import unreal
MEL = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

def mpc():
    p = unreal.load_asset('/Eidoverse/FX/MPC_EidoverseWater')
    if not p: p = AT.create_asset('MPC_EidoverseWater', '/Eidoverse/FX', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    sc = list(p.get_editor_property('scalar_parameters')); have = [str(s.get_editor_property('parameter_name')) for s in sc]
    for n in ('WaveT', 'OriginX', 'OriginY'):
        if n not in have:
            s = unreal.CollectionScalarParameter(); s.set_editor_property('parameter_name', n); s.set_editor_property('default_value', 0.0); sc.append(s)
    p.set_editor_property('scalar_parameters', sc); unreal.EditorAssetLibrary.save_loaded_asset(p); return p

WAVES = r"""
// shared/waves.js, verbatim law. P: absolute world cm; returns the surface's rise in metres at wire (x, z).
struct W {
  float cyc(float w) { const float T = 6.2831853; return T * max(1.0, round(w * 600.0 / T)) / 600.0; }
  float sm(float a, float b, float x) { float t = saturate((x - a) / (b - a)); return t * t * (3.0 - 2.0 * t); }
  float crest(float p) { return sin(p) + 0.18 * sin(2.0 * p); }
  float rise(float x, float z, float t, float kind, float2 dir, float depth, float ws) {
    if (kind > 0.75 && dot(dir, dir) > 0.25) {
      float o = x * dir.x + z * dir.y, oc = z * dir.x - x * dir.y;
      float p1 = o * 0.2 - t * cyc(sqrt(9.8 * 0.2)) + sin(oc * 0.045) * 1.4;
      float p2 = o * 0.33 + oc * 0.12 - t * cyc(sqrt(9.8 * 0.33));
      float amp = 0.2 + 0.4 * sm(-1.0, 5.0, depth);
      return amp * crest(p1) + amp * 0.45 * crest(p2);
    }
    if (kind > 0.25 && dot(dir, dir) > 0.25) { float a = x * dir.x + z * dir.y; return 0.025 * sin((a - t * ws) * 1.4); }
    return 0.012 * sin(x * 0.9 + z * 0.4 - t * cyc(0.8));
  }
};
W w;
float x = -(P.y - OY) / 100.0, z = (P.x - OX) / 100.0;
float depth = C.r * 10.0, kind = C.b * 2.0 / 2.0;
"""
WPO = WAVES + "return float3(0, 0, w.rise(x, z, T, C.b, Dir, depth, WS) * 100.0);"
NORMAL = WAVES + """
float e = 0.25;
float h0 = w.rise(x, z, T, C.b, Dir, depth, WS);
float hx = w.rise(x + e, z, T, C.b, Dir, depth, WS), hz = w.rise(x, z + e, T, C.b, Dir, depth, WS);
// wire +x is UE -Y, wire +z is UE +X
float dX = (hz - h0) / e, dY = -(hx - h0) / e;
return normalize(float3(-dX, -dY, 1.0));"""
SHADE = """
float depth = C.r * 10.0, shore = C.g * 20.0;
float deep = smoothstep(0.0, 3.5, depth);
float3 shallow = lerp(Col, float3(0.75, 0.88, 0.85), 0.55);
float foam = (1.0 - smoothstep(0.0, 1.1, shore)) * (0.35 + 0.65 * step(0.75, C.b));
return lerp(lerp(shallow, Col, deep), float3(0.95, 0.97, 0.98), foam * 0.8);"""
OPAC = """float depth = C.r * 10.0, shore = C.g * 20.0;
float foam = 1.0 - smoothstep(0.0, 1.1, shore);
return saturate(lerp(0.3, Op, smoothstep(0.0, 2.5, depth)) + foam * 0.35);"""

def custom(m, code, ins, out_type, x, y):
    n = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, x, y)
    n.set_editor_property('output_type', out_type); n.set_editor_property('code', code)
    lst = []
    for nm in ins:
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); lst.append(ci)
    n.set_editor_property('inputs', lst); return n

coll = mpc()
path = '/Eidoverse/FX/M_EidoverseLevelWater'
m = unreal.load_asset(path)
if not m: m = AT.create_asset('M_EidoverseLevelWater', '/Eidoverse/FX', unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
m.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
m.set_editor_property('translucency_lighting_mode', unreal.TranslucencyLightingMode.TLM_SURFACE)
m.set_editor_property('tangent_space_normal', False)
m.set_editor_property('two_sided', True)
MEL.delete_all_material_expressions(m)
wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -1400, 0)
vc = MEL.create_material_expression(m, unreal.MaterialExpressionVertexColor, -1400, 120)
uv1 = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1400, 240); uv1.set_editor_property('coordinate_index', 1)
uv2 = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1400, 360); uv2.set_editor_property('coordinate_index', 2)
ws = MEL.create_material_expression(m, unreal.MaterialExpressionComponentMask, -1200, 360); ws.set_editor_property('r', True); ws.set_editor_property('g', False)
MEL.connect_material_expressions(uv2, '', ws, '')
def cp(name, y):
    c = MEL.create_material_expression(m, unreal.MaterialExpressionCollectionParameter, -1400, y)
    c.set_editor_property('collection', coll); c.set_editor_property('parameter_name', name); return c
t, ox, oy = cp('WaveT', 480), cp('OriginX', 560), cp('OriginY', 640)
col = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -1400, 760)
col.set_editor_property('parameter_name', 'Color'); col.set_editor_property('default_value', unreal.LinearColor(0.012, 0.1, 0.16, 1))
op = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1400, 880)
op.set_editor_property('parameter_name', 'Opacity'); op.set_editor_property('default_value', 0.82)
wave_ins = ['P', 'C', 'Dir', 'WS', 'T', 'OX', 'OY']
def wire_wave(n):
    for nm, src in (('P', wp), ('C', vc), ('Dir', uv1), ('WS', ws), ('T', t), ('OX', ox), ('OY', oy)):
        MEL.connect_material_expressions(src, '', n, nm)
wpo = custom(m, WPO, wave_ins, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 0); wire_wave(wpo)
nrm = custom(m, NORMAL, wave_ins, unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 200); wire_wave(nrm)
shade = custom(m, SHADE, ['C', 'Col'], unreal.CustomMaterialOutputType.CMOT_FLOAT3, -800, 400)
MEL.connect_material_expressions(vc, '', shade, 'C'); MEL.connect_material_expressions(col, '', shade, 'Col')
opac = custom(m, OPAC, ['C', 'Op'], unreal.CustomMaterialOutputType.CMOT_FLOAT1, -800, 600)
MEL.connect_material_expressions(vc, '', opac, 'C'); MEL.connect_material_expressions(op, '', opac, 'Op')
rough = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 760); rough.set_editor_property('r', 0.06)
spec = MEL.create_material_expression(m, unreal.MaterialExpressionConstant, -800, 840); spec.set_editor_property('r', 0.5)
MEL.connect_material_property(wpo, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
MEL.connect_material_property(nrm, '', unreal.MaterialProperty.MP_NORMAL)
MEL.connect_material_property(shade, '', unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(opac, '', unreal.MaterialProperty.MP_OPACITY)
MEL.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
MEL.connect_material_property(spec, '', unreal.MaterialProperty.MP_SPECULAR)
MEL.recompile_material(m)
unreal.EditorAssetLibrary.save_loaded_asset(m)
unreal.log_warning('LEVELWATER_MAT ok')
unreal.SystemLibrary.quit_editor()
