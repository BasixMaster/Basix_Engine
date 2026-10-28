"""Pinned Vulkan boundary/instance fixture gate with independent numeric checks.

All tools and inputs are supplied locally. Raw logs and full numeric receipts
remain in --output; the aggregate receipt contains no private local paths.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import itertools
import json
import os
import re
import subprocess
import sys

from ray_boundary_oracle import validate_result
from run_visible_validation import scan, sha

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = set(itertools.product((.001, 1., 1000.), range(6), (-1, 1), range(5), (0, 1)))


def require(value, message):
    if not value:
        raise ValueError(message)


def contract(result, *, bad_transform=False, bad_spawn=False, omitted=False, outcome='pass'):
    settings = dict(candidate='world_triangle_metadata_v1', cases=360,
                    minimum_exact_vertex_hits=12, minimum_exact_edge_hits=12,
                    bad_transform=bad_transform, bad_spawn=bad_spawn, omit_dispatch=omitted,
                    validation_enabled=True, validation_errors=0, validation_warnings=0)
    require(all(result.get(k) == v for k, v in settings.items()),
            'Boundary receipt settings or validation coverage differs')
    checked = validate_result(result, expected_identities=EXPECTED,
                              require_passed=outcome == 'pass', verify_host_flags=True)
    require(result.get('exact_vertex_hits') == checked['observed_exact_vertices'] and
            result.get('exact_edge_hits') == checked['observed_exact_edges'],
            'Actual hardware boundary counts differ from serialized barycentrics')
    if outcome == 'pass':
        require(checked['observed_exact_vertices'] >= 12 and checked['observed_exact_edges'] >= 12,
                'Actual hardware vertex/edge observations are insufficient')
    elif outcome == 'all-fail':
        require(checked['failed_cases'] == 360, 'Fault did not reject every poisoned/invalid fixture')
    else:
        require(checked['failed_cases'] > 0, 'Bad transform failed to expose a geometry/query disagreement')
    return checked


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'shader', 'slangc', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    exe, shader, slangc, layer, out = (p.resolve() for p in
        (args.exe, args.shader, args.slangc, args.validation_layer_dir, args.output))
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Use a fresh empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs = dict(executable=exe, shader=shader, slangc=slangc,
                  validation_dll=layer / 'VkLayer_khronos_validation.dll',
                  validation_manifest=layer / 'VkLayer_khronos_validation.json',
                  harness=Path(__file__).resolve(), oracle=ROOT / 'tools/ray_boundary_oracle.py',
                  scanner=ROOT / 'tools/run_visible_validation.py')
    for path in ('src/progressive_gpu.cpp', 'src/progressive_boundary_tests.inl',
                 'src/progressive_ray_tests.inl', 'src/ray_spawn.cpp',
                 'include/basix/ray_spawn.hpp', 'include/basix/progressive_gpu_abi.hpp',
                 'shaders/progressive_pt.slang', 'shaders/basix_ray_spawn.slang'):
        inputs[path] = ROOT / path
    require(shader.name == 'progressive_pt.spv' and all(p.is_file() for p in inputs.values()),
            'Supplied tool/shader/validation/source file missing')
    require(sha(inputs['validation_dll']) == lock['Vulkan-ValidationLayers']['dll_sha256'],
            'Validation DLL differs from the tested pin')
    version = subprocess.run([str(slangc), '-version'], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=30)
    (out / 'slang-version.log').write_text(version.stdout, encoding='utf-8')
    match = re.search(r'\b(\d+\.\d+\.\d+)\b', version.stdout)
    require(not version.returncode and match and match.group(1) == lock['Slang']['version'],
            'Slang version differs from the tested pin')
    before = {name: sha(path) for name, path in inputs.items()}
    env = dict(os.environ, VK_LAYER_PATH=str(layer), VK_LOADER_LAYERS_DISABLE='~implicit~',
               VK_VALIDATION_VALIDATE_SYNC='1')
    records, results, numeric = [], {}, {}

    def run(name, directory, *, outcome='pass', extra=(), trace=False):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        command = [str(exe), '--validate', '--ray-boundary-tests', '--shader-dir', str(directory),
                   '--output', str(out / name), *extra]
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=300)
        (out / (name + '.log')).write_text(process.stdout, encoding='utf-8')
        require(process.returncode == (0 if outcome == 'pass' else 1),
                name + ': unexpected exit; inspect local process log')
        reviewed = scan(process.stdout, name, trace)
        result = json.loads((out / name / 'ray-boundary-result.json').read_text(encoding='utf-8'))
        checked = contract(result, bad_transform='--bad-transform' in extra,
                           bad_spawn='--bad-spawn' in extra, omitted='--omit-dispatch' in extra,
                           outcome=outcome)
        diagnostic = 'PASS ray boundary fixtures 360' if outcome == 'pass' else 'FAIL: Ray boundary fixture gate failed'
        require(diagnostic in process.stdout, name + ': acceptance/rejection diagnostic missing')
        records.append(dict(name=name, exit_code=process.returncode, expected_outcome_verified=True,
                            outcome='candidate pass' if outcome == 'pass' else 'expected control rejection',
                            failed_fixtures=checked['failed_cases'], numeric_readback_rechecked=True,
                            observed_exact_vertices=checked['observed_exact_vertices'],
                            observed_exact_edges=checked['observed_exact_edges'],
                            reviewed_loader_implicit_disable_messages=reviewed))
        results[name] = result
        numeric[name] = checked

    run('candidate', shader.parent, trace=True)
    run('bad-transform', shader.parent, outcome='fail', extra=('--bad-transform',))
    run('invalid-spawn', shader.parent, outcome='all-fail', extra=('--bad-spawn',))
    run('omitted-dispatch', shader.parent, outcome='all-fail', extra=('--omit-dispatch',))
    scratch = out / 'no-op-shader'
    scratch.mkdir()
    source = scratch / 'no-op.slang'
    source.write_text('// MIT: isolated poisoned-output rejection control.\n[numthreads(8,8,1)] void computeMain(uint3 id:SV_DispatchThreadID) {}\n', encoding='utf-8')
    target = scratch / 'progressive_pt.spv'
    compiled = subprocess.run([str(slangc), str(source), '-entry', 'computeMain', '-stage', 'compute',
        '-target', 'spirv', '-profile', 'spirv_1_5', '-emit-spirv-directly', '-fvk-use-entrypoint-name',
        '-fvk-use-c-layout', '-matrix-layout-row-major', '-fp-mode', 'precise', '-o', str(target)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=60)
    (out / 'no-op-compile.log').write_text(compiled.stdout, encoding='utf-8')
    require(not compiled.returncode and target.is_file(), 'No-op control shader compilation failed')
    run('no-op-shader', scratch, outcome='all-fail')
    after = {name: sha(path) for name, path in inputs.items()}
    require(before == after, 'Frozen executable/shader/compiler/validation/source changed during validation')
    receipt = dict(checked_on='2026-10-28', status='passed', harness_checks_passed=True,
        cases=records, results=results, independent_numeric=numeric,
        input_sha256=before, input_hashes_unchanged=True, no_op_shader_sha256=sha(target),
        slang_version=match.group(1), loader_confirmed_validation=True,
        synchronization_validation_requested=True,
        full_process_diagnostics='zero unreviewed backend/validation warnings/errors including startup and teardown; exact loader implicit-disable notices counted separately',
        limits=['One RTX4090 configuration and one TLAS instance per fixture',
                'Six frozen identity/rotation/nonuniform/shear/mirror/translation input families, three scales and five target families',
                'World-space triangle metadata independently baked from the object BLAS; no general instance remapping or motion',
                'Empirical traversal allowance, not a universal transform or Vulkan hardware error proof',
                'No cross-device, performance, subnormal arithmetic or rendered temporal quality acceptance'])
    (out / 'ray-boundary-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', harness_checks=5, candidate_fixtures=360,
                         actual_shader_fault_controls=1,
                         exact_vertices=numeric['candidate']['observed_exact_vertices'],
                         exact_edges=numeric['candidate']['observed_exact_edges'])))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
