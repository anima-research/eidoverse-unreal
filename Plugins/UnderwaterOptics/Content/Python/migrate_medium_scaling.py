"""Idempotent editor migration: authored-medium scaling for the shared water optics.

Adds MPC_Water vectors WaterExtinctionScale and WaterScatterTint (defaults 1,1,1 =
the reference water) and feeds them into every production material whose Custom node embeds
FWaterOptics (fields ExtScale / ScatTint in WaterOptics.ush). The Eidoverse client
writes these vectors from the world's `water` component.

Run with the editor, not a commandlet:
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
Prints MEDIUM_SCALING_DONE <n materials> or MEDIUM_SCALING_FAILED.
"""
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
from editor_toolset.toolsets.material import MaterialTools

MEL = unreal.MaterialEditingLibrary
MPC_PATH = '/Game/Underwater/Materials/MPC_Water'
MATERIALS = [
    '/Game/Underwater/Materials/M_Glass',
    '/Game/Underwater/Materials/M_WaterOpticsView',
    '/Game/Underwater/Materials/M_WaterOpticsLocalLight',
    '/Game/Underwater/Materials/LF_SunSpectral',
    '/Game/Underwater/Tests/M_OceanRegression',
    '/Game/Underwater/SpectralPrototype/M_SpectralOptics',
    '/Game/Underwater/SpectralPrototype/M_SpectralLocalLight',
]
PINS = {'ExtScale': 'WaterExtinctionScale', 'ScatTint': 'WaterScatterTint'}
INIT = 'FWaterOptics w;'
ADDED = ' w.ExtScale=ExtScale.rgb; w.ScatTint=ScatTint.rgb;'


def ensure_mpc_vectors(mpc):
    params = list(mpc.get_editor_property('vector_parameters'))
    names = {str(p.get_editor_property('parameter_name')) for p in params}
    changed = False
    for name in PINS.values():
        if name not in names:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property('parameter_name', name)
            p.set_editor_property('default_value', unreal.LinearColor(1, 1, 1, 0))
            params.append(p)
            changed = True
    if changed:
        mpc.set_editor_property('vector_parameters', params)
        unreal.EditorAssetLibrary.save_loaded_asset(mpc)


def migrate(path, mpc):
    mat = unreal.load_asset(path)
    if not mat:
        raise RuntimeError('missing material ' + path)
    customs = [e for e in MaterialTools.get_expressions(mat)
               if isinstance(e, unreal.MaterialExpressionCustom) and INIT in e.get_editor_property('code')]
    if not customs:
        raise RuntimeError('no FWaterOptics custom node in ' + path)
    for custom in customs:
        code = custom.get_editor_property('code')
        if 'w.ExtScale' not in code:
            inputs = list(custom.get_editor_property('inputs'))
            have = {str(i.get_editor_property('input_name')) for i in inputs}
            for pin in PINS:
                if pin not in have:
                    ci = unreal.CustomInput()
                    ci.set_editor_property('input_name', pin)
                    inputs.append(ci)
            custom.set_editor_property('inputs', inputs)
            custom.set_editor_property('code', code.replace(INIT, INIT + ADDED, 1))
        # Connect after the code edit: Custom rebuilds pins when its code changes.
        pins = {str(i.get_editor_property('input_name')) for i in custom.get_editor_property('inputs')}
        if not all(pin in pins for pin in PINS):
            continue  # node initializes ExtScale/ScatTint in code (test materials)
        existing = {}
        for e in MaterialTools.get_expressions(mat):
            if isinstance(e, unreal.MaterialExpressionCollectionParameter):
                existing.setdefault(str(e.get_editor_property('parameter_name')), e)
        for pin, param in PINS.items():
            node = existing.get(param)
            if not node:
                node = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter)
                node.set_editor_property('collection', mpc)
                node.set_editor_property('parameter_name', param)
            if not MEL.connect_material_expressions(node, '', custom, pin):
                raise RuntimeError('could not connect %s in %s' % (pin, path))
    MEL.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return len(customs)


try:
    mpc = unreal.load_asset(MPC_PATH)
    ensure_mpc_vectors(mpc)
    total = sum(migrate(p, mpc) for p in MATERIALS)
    unreal.log_warning('MEDIUM_SCALING_DONE materials=%d custom_nodes=%d' % (len(MATERIALS), total))
except Exception:
    unreal.log_error('MEDIUM_SCALING_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
