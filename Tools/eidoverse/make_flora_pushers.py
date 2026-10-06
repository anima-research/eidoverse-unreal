"""Flora pushers + the keep(d) distance law (flora.js / vegetation.js) in the two flora
materials: MPC_EidoverseFlora carries 4 pushers (world cm x, y, z, radius); a Custom
node chained after each material's existing wind WPO parts plants around bodies, and
for ground cover thins by keep(d) = ((90 - d)/75)^2.5 with survivors growing to 1.7x.
Idempotent: re-running replaces the chained node."""
import unreal
MEL = unreal.MaterialEditingLibrary
MPC_PATH = '/Eidoverse/FX/MPC_EidoverseFlora'

def collection():
    p = unreal.load_asset(MPC_PATH)
    if not p:
        p = unreal.AssetToolsHelpers.get_asset_tools().create_asset('MPC_EidoverseFlora', '/Eidoverse/FX', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    vectors = list(p.get_editor_property('vector_parameters'))
    have = [str(v.get_editor_property('parameter_name')) for v in vectors]
    for i in range(4):
        n = 'Pusher%d' % i
        if n not in have:
            v = unreal.CollectionVectorParameter(); v.set_editor_property('parameter_name', n); v.set_editor_property('default_value', unreal.LinearColor(0, 0, 0, 0)); vectors.append(v)
    p.set_editor_property('vector_parameters', vectors)
    unreal.EditorAssetLibrary.save_loaded_asset(p)
    return p

PUSH = """
float4 Ps[4] = {P0, P1, P2, P3};
float2 push = 0;
for (int i = 0; i < 4; i++)
{
    if (Ps[i].w <= 0) continue;
    float2 dl = Root.xy - Ps[i].xy; float l = length(dl);
    float s = 1 - smoothstep(0, Ps[i].w, min(l, length(P.xy - Ps[i].xy)));
    push += dl / max(l, 30) * s * s * 135;
}
float pl = length(push);
if (pl > .01) push *= 60 * tanh(pl / 60) / pl;
"""
COVER = """float tip = saturate(UV.y);
float d = distance(Root, Camera) / 100;
float keep = pow(saturate((90 - d) / 75), 2.5);
float r = frac(sin(dot(floor(Root.xy), float2(12.9898, 78.233))) * 43758.5453);
if (r > keep) return Root - P;
float grow = lerp(1, 1.7, smoothstep(15, 90, d));
""" + PUSH + """return Base + (P - Root) * (grow - 1) + float3(push, 0) * (0.3 + 0.7 * tip) * tip;"""
FLORA = """float tip = saturate(UV.y);
float3 Root = P;
""" + PUSH + """return Base + float3(push, 0) * tip * tip;"""

def chain(path, code, with_root, coll):
    m = unreal.load_asset(path)
    base = MEL.get_material_property_input_node(m, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if base and base.get_editor_property('description') == 'EidoPushers':
        # re-run: the chained node's Base input is the original wind node
        ins = MEL.get_inputs_for_material_expression(m, base); base_orig = ins[0]
        MEL.delete_material_expression(m, base); base = base_orig
    old = base.get_editor_property('code')
    if '4500-distance' in old:   # the old 28-45 m fade gives way to keep(d)
        base.set_editor_property('code', old.replace('float fade=saturate((4500-distance(P,Camera))/1700);', 'float fade=1;'))
    n = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -200, 400)
    n.set_editor_property('description', 'EidoPushers')
    n.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    names = ['Base', 'P', 'UV', 'P0', 'P1', 'P2', 'P3'] + (['Root', 'Camera'] if with_root else [])
    ins = []
    for nm in names:
        ci = unreal.CustomInput(); ci.set_editor_property('input_name', nm); ins.append(ci)
    n.set_editor_property('inputs', ins)
    n.set_editor_property('code', code)
    MEL.connect_material_expressions(base, '', n, 'Base')
    wp = MEL.create_material_expression(m, unreal.MaterialExpressionWorldPosition, -600, 400); MEL.connect_material_expressions(wp, '', n, 'P')
    uv = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -600, 480); MEL.connect_material_expressions(uv, '', n, 'UV')
    for i in range(4):
        c = MEL.create_material_expression(m, unreal.MaterialExpressionCollectionParameter, -600, 560 + 60 * i)
        c.set_editor_property('collection', coll); c.set_editor_property('parameter_name', 'Pusher%d' % i)
        MEL.connect_material_expressions(c, '', n, 'P%d' % i)
    if with_root:
        root = MEL.create_material_expression(m, unreal.MaterialExpressionObjectPositionWS, -600, 820); MEL.connect_material_expressions(root, '', n, 'Root')
        cam = MEL.create_material_expression(m, unreal.MaterialExpressionCameraPositionWS, -600, 880); MEL.connect_material_expressions(cam, '', n, 'Camera')
    MEL.connect_material_property(n, '', unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    unreal.log_warning('FLORA_MAT ok %s inputs=%d' % (path, len(MEL.get_inputs_for_material_expression(m, n))))

coll = collection()
chain('/Game/Underwater/Avatars/Materials/M_EidoverseGroundCover', COVER, True, coll)
chain('/Game/Underwater/Avatars/Materials/M_EidoverseFlora', FLORA, False, coll)
unreal.SystemLibrary.quit_editor()
