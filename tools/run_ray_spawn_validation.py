"""Bounded layered-triangle Vulkan ray-spawn acceptance with actual fault controls.

Uses supplied local tools; no downloads or global environment changes. The
sanitized receipt separates candidate acceptance from expected control failures.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import itertools
import json
import math
import os
import re
import subprocess
import sys

from run_visible_validation import scan, sha

ROOT = Path(__file__).resolve().parents[1]
SCALES = (.001, 1., 1000.)
TRANSLATIONS = (0., 1048576., -16777216.)
EXPECTED = set(itertools.product(SCALES, TRANSLATIONS, (-1, 1), (0, 1), range(5)))


def require(value, message):
    if not value:
        raise ValueError(message)


def contract(result, bounded, rebase, outcome, bad_spawn=False, omitted=False):
    settings = dict(candidate='triangle_error_box_v1', bounded_rays=bounded,
                    camera_relative_rebase=rebase, cases=180, bad_spawn=bad_spawn, omit_dispatch=omitted,
                    secondary_cases=72, shadow_cases=108, validation_enabled=True,
                    validation_errors=0, validation_warnings=0)
    require(all(result.get(k) == v for k, v in settings.items()),
            'Ray-spawn settings, identity or validation coverage differs')
    fixtures = result['fixtures']
    require(len(fixtures) == 180, 'Ray fixture coverage differs')
    identities = []
    for row in fixtures:
        identity = tuple(row[k] for k in ('scale', 'world_translation', 'sign', 'oblique', 'kind'))
        require(identity in EXPECTED, 'Unexpected ray fixture')
        identities.append(identity)
        require(isinstance(row['passed'], bool), 'Fixture outcome must be boolean')
        require(math.isfinite(row['gap']) and
                math.isclose(row['gap'], 1e-5 * row['scale'], rel_tol=1e-12, abs_tol=1e-25),
                'Ray fixture physical gap differs')
        expected_index = 1 if row['kind'] in (0, 2) else (0 if row['kind'] == 4 else -1)
        require(row['expected_index'] == expected_index, 'Analytic expected plane differs')
        # If numeric readback is serialized, derive pass/fail from it. Nonfinite
        # poisoned components are represented as JSON null by the host.
        require('hit' in row and 'spawn' in row, 'Numeric GPU readback missing')
        require('actual_uploaded_gap' in row and math.isfinite(row['actual_uploaded_gap']), 'Uploaded gap missing/nonfinite')
        if outcome == 'pass' and row['kind'] in (0, 2):
            require(row['actual_uploaded_gap'] > 0, 'Candidate physical gap collapsed')
        if 'hit' in row and 'spawn' in row:
            hit, spawn = row['hit'], row['spawn']
            require(len(hit) == len(spawn) == 4, 'Readback shape differs')
            finite = all(isinstance(x, (int, float)) and math.isfinite(x) for x in hit + spawn)
            passed = finite and hit[0] == 0 and hit[1] == expected_index and hit[3] == 1 and spawn[3] >= 0
            if row['kind'] == 0 and finite:
                local_z = spawn[2] - (0 if rebase else row['world_translation'])
                require(isinstance(row['local_spawn_z'], (int, float)) and math.isclose(row['local_spawn_z'], local_z, rel_tol=1e-10, abs_tol=1e-12), 'Local spawn coordinate differs')
                passed = passed and 0 < row['sign'] * local_z < row['actual_uploaded_gap']
            if expected_index == 1 and finite:
                passed = passed and hit[2] > 0
            require(bool(passed) == row['passed'], 'Serialized GPU readback disagrees with fixture status')
    require(len(set(identities)) == 180 and set(identities) == EXPECTED,
            'Ray fixture Cartesian product is incomplete or duplicated')
    failed = sum(not row['passed'] for row in fixtures)
    require(result['failed_cases'] == failed, 'Fixture failure count differs')
    if outcome == 'pass':
        require(failed == 0 and result['status'] == 'passed', 'Candidate ray-spawn gate failed')
    else:
        require(failed > 0 and result['status'] == 'failed', 'Control failed to expose the expected defect')
        if outcome == 'all-fail':
            require(failed == 180, 'Poisoned or invalid output did not reject every fixture')
    return all('hit' in row and 'spawn' in row for row in fixtures)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'shader', 'slangc', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    exe, shader, slangc, layer, out = (p.resolve() for p in
        (args.exe, args.shader, args.slangc, args.validation_layer_dir, args.output))
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())),
            'Use a fresh empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs = dict(executable=exe, shader=shader, slangc=slangc,
                  validation_dll=layer / 'VkLayer_khronos_validation.dll',
                  validation_manifest=layer / 'VkLayer_khronos_validation.json',
                  harness=Path(__file__).resolve(),
                  scanner=ROOT / 'tools/run_visible_validation.py')
    for path in ('src/progressive_gpu.cpp', 'src/progressive_ray_tests.inl',
                 'src/ray_spawn.cpp', 'include/basix/ray_spawn.hpp',
                 'include/basix/progressive_gpu_abi.hpp',
                 'shaders/progressive_pt.slang', 'shaders/basix_ray_spawn.slang'):
        inputs[path] = ROOT / path
    require(shader.name == 'progressive_pt.spv' and all(p.is_file() for p in inputs.values()),
            'Pinned executable/shader/compiler/validation/source input missing')
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
    records, results = [], {}

    def run(name, directory, bounded=True, rebase=True, outcome='pass', extra=(), trace=False):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        command = [str(exe), '--validate', '--ray-spawn-tests', '--shader-dir', str(directory),
                   '--output', str(out / name)]
        if bounded:
            command.append('--bounded-rays')
        if not rebase:
            command.append('--unrebased')
        command.extend(extra)
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=300)
        (out / (name + '.log')).write_text(process.stdout, encoding='utf-8')
        require(process.returncode == (0 if outcome == 'pass' else 1),
                name + ': unexpected exit; inspect local log')
        reviewed = scan(process.stdout, name, trace)
        result = json.loads((out / name / 'ray-spawn-result.json').read_text(encoding='utf-8'))
        numeric = contract(result, bounded, rebase, outcome, '--bad-spawn' in extra, '--omit-dispatch' in extra)
        require(('PASS ray spawn fixtures 180' if outcome == 'pass' else 'FAIL: Ray spawn fixture gate failed')
                in process.stdout, name + ': expected acceptance/rejection diagnostic missing')
        records.append(dict(name=name, exit_code=process.returncode, expected_outcome_verified=True,
                            outcome='candidate pass' if outcome == 'pass' else 'expected control rejection',
                            failed_fixtures=result['failed_cases'], numeric_readback_rechecked=numeric,
                            reviewed_loader_implicit_disable_messages=reviewed))
        results[name] = result

    run('candidate', shader.parent, trace=True)
    run('legacy-offset', shader.parent, bounded=False, outcome='fail')
    run('unrebased', shader.parent, rebase=False, outcome='fail')
    run('omitted-dispatch', shader.parent, outcome='all-fail', extra=('--omit-dispatch',))
    run('invalid-spawn', shader.parent, outcome='all-fail', extra=('--bad-spawn',))
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
    require(not compiled.returncode and target.is_file(), 'No-op shader compilation failed')
    run('no-op-shader', scratch, outcome='all-fail')
    after = {name: sha(path) for name, path in inputs.items()}
    require(before == after, 'Executable/shader/compiler/validation/source input changed during validation')
    receipt = dict(checked_on='2026-10-28', status='passed', harness_checks_passed=True,
        cases=records, results=results, input_sha256=before, input_hashes_unchanged=True,
        no_op_shader_sha256=sha(target), slang_version=match.group(1),
        loader_confirmed_validation=True, synchronization_validation_requested=True,
        full_process_diagnostics='zero unreviewed backend/validation warnings/errors including startup and teardown; exact loader implicit-disable notices counted separately',
        limits=['One RTX4090 driver configuration and identity TLAS instance',
                'Three scales, three translations, two sides and two directions; layered triangles only',
                'Traversal extent allowance is empirical, not a universal Vulkan hardware error bound',
                'No arbitrary transforms, subnormal arithmetic, pre-rounded/unrepresentable geometry, temporal or performance acceptance'])
    (out / 'ray-spawn-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', harness_checks=len(records), candidate_fixtures=180,
                         secondary=72, shadow=108, actual_shader_fault_controls=1)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
