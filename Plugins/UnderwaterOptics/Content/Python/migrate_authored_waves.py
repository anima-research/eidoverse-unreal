"""Idempotent editor migration: authored analytic waves in the spectral ocean.

Adds MPC_Water parameters AuthoredWave0..7 (amplitude cm, wavenumber rad/cm,
dirX, dirY), AuthoredWavePhase0..1 and scalar AuthoredWaveCount (default 0 = no
authored waves, the ocean looks as before), and wires them into every Custom node
that declares an FSpectralField (fields AW/AWP0/AWP1/AWN in SpectralField.ush).
ASpectralOcean writes these each frame from the Eidoverse `water` component.

Run with the editor, not a commandlet:
  UnrealEditor Test1.uproject -ExecutePythonScript=<this file>
Prints AUTHORED_WAVES_DONE or AUTHORED_WAVES_FAILED.
"""
import re
import traceback
import unreal

unreal.EditorPythonScripting.set_keep_python_script_alive(True)
from editor_toolset.toolsets.material import MaterialTools

MEL = unreal.MaterialEditingLibrary
MPC_PATH = '/Game/Underwater/Materials/MPC_Water'
MATERIALS = [
    '/Game/Underwater/SpectralPrototype/M_SpectralOptics',
    '/Game/Underwater/SpectralPrototype/M_SpectralSurface',
    '/Game/Underwater/SpectralPrototype/M_SpectralUnderside',
    '/Game/Underwater/SpectralPrototype/M_SpectralLocalLight',
]
# pin -> (MPC parameter, is_scalar)
PINS = {f'AW{i}': (f'AuthoredWave{i}', False) for i in range(8)}
PINS.update({'AWP0': ('AuthoredWavePhase0', False), 'AWP1': ('AuthoredWavePhase1', False),
             'AWN': ('AuthoredWaveCount', True)})
DECL = re.compile(r'FSpectralField\s+(\w+)\s*;')


def ensure_mpc(mpc):
    vectors = list(mpc.get_editor_property('vector_parameters'))
    scalars = list(mpc.get_editor_property('scalar_parameters'))
    vnames = {str(p.get_editor_property('parameter_name')) for p in vectors}
    snames = {str(p.get_editor_property('parameter_name')) for p in scalars}
    changed = False
    for param, scalar in PINS.values():
        if scalar and param not in snames:
            p = unreal.CollectionScalarParameter()
            p.set_editor_property('parameter_name', param)
            p.set_editor_property('default_value', 0.0)
            scalars.append(p)
            changed = True
        elif not scalar and param not in vnames:
            p = unreal.CollectionVectorParameter()
            p.set_editor_property('parameter_name', param)
            p.set_editor_property('default_value', unreal.LinearColor(0, 0, 1, 0))
            vectors.append(p)
            changed = True
    if changed:
        mpc.set_editor_property('vector_parameters', vectors)
        mpc.set_editor_property('scalar_parameters', scalars)
        unreal.EditorAssetLibrary.save_loaded_asset(mpc)


def init_code(name):
    code = ''.join(f'{name}.AW[{i}]=AW{i};' for i in range(8))
    return code + f'{name}.AWP0=AWP0;{name}.AWP1=AWP1;{name}.AWN=AWN;'


def migrate(path, mpc):
    mat = unreal.load_asset(path)
    if not mat:
        raise RuntimeError('missing material ' + path)
    customs = [e for e in MaterialTools.get_expressions(mat)
               if isinstance(e, unreal.MaterialExpressionCustom) and DECL.search(e.get_editor_property('code'))]
    if not customs:
        raise RuntimeError('no FSpectralField custom node in ' + path)
    for custom in customs:
        code = custom.get_editor_property('code')
        name = DECL.search(code).group(1)
        if f'{name}.AWN=' not in code:
            inputs = list(custom.get_editor_property('inputs'))
            have = {str(i.get_editor_property('input_name')) for i in inputs}
            for pin in PINS:
                if pin not in have:
                    ci = unreal.CustomInput()
                    ci.set_editor_property('input_name', pin)
                    inputs.append(ci)
            custom.set_editor_property('inputs', inputs)
            decl = DECL.search(code).group(0)
            custom.set_editor_property('code', code.replace(decl, decl + init_code(name), 1))
        existing = {}
        for e in MaterialTools.get_expressions(mat):
            if isinstance(e, unreal.MaterialExpressionCollectionParameter):
                existing.setdefault(str(e.get_editor_property('parameter_name')), e)
        for pin, (param, _) in PINS.items():
            node = existing.get(param)
            if not node:
                node = MEL.create_material_expression(mat, unreal.MaterialExpressionCollectionParameter)
                node.set_editor_property('collection', mpc)
                node.set_editor_property('parameter_name', param)
                existing[param] = node
            if not MEL.connect_material_expressions(node, '', custom, pin):
                raise RuntimeError('could not connect %s in %s' % (pin, path))
    errors = MEL.recompile_material(mat)
    if errors:
        raise RuntimeError('%s failed to compile: %s' % (path, ' | '.join(str(e) for e in errors)))
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return len(customs)


try:
    mpc = unreal.load_asset(MPC_PATH)
    ensure_mpc(mpc)
    total = sum(migrate(p, mpc) for p in MATERIALS)
    unreal.log_warning('AUTHORED_WAVES_DONE materials=%d custom_nodes=%d' % (len(MATERIALS), total))
except Exception:
    unreal.log_error('AUTHORED_WAVES_FAILED\n' + traceback.format_exc())
finally:
    unreal.SystemLibrary.quit_editor()
