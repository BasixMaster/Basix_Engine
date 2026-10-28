"""Run the bounded transform-ray candidate and six GPU rejection controls.

Local supplied tools only. Full process logs/raw receipts stay under --output;
transform-ray-results.json is the path-free acceptance aggregate. This harness
does not turn conditional forward/object/inverse policies into hardware proofs.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import sys
import time

from run_visible_validation import scan, sha
from transform_uncertainty_oracle import verify_receipt

ROOT = Path(__file__).resolve().parents[1]
FAULTS = ('zero-budget', 'negative-budget', 'origin', 'direction', 'omit')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def contract(result, numeric, fault=''):
    """Receipt metadata and oracle outcome, independently from host status."""
    expected = dict(status='failed' if fault else 'passed', cases=96,
                    failed_cases=96 if fault else 0, fault=fault,
                    origin_limit=32, max_distance=128, root_bytes=208,
                    triangle_bytes=80, validation_enabled=True,
                    validation_errors=0, validation_warnings=0)
    require(all(type(result.get(key)) is type(value) and result[key] == value
                for key, value in expected.items()),
            'Transform metadata/settings/expected rejection differs')
    require(isinstance(result.get('fixtures'), list) and len(result['fixtures']) == 96,
            'Transform fixture count differs')
    require(numeric.get('cases') == 96 and numeric.get('failed_cases') == expected['failed_cases'],
            'Independent failed count differs from host/required control outcome')
    require(numeric.get('numeric_readback_rechecked') is True and numeric.get('host_disagreements') == [],
            'Independent outcomes disagree with host row flags')
    if not fault:
        for key in ('exact_vertices', 'exact_edges'):
            require(type(result.get(key)) is int and 12 <= result[key] <= 96,
                    'Required exact boundary-hit coverage missing')
        require(result['exact_vertices'] == numeric.get('observed_exact_vertices') and
                result['exact_edges'] == numeric.get('observed_exact_edges'),
                'Exact boundary observation accounting differs from independent oracle')
    return dict(cases=96, failed_cases=numeric['failed_cases'],
                numeric_readback_rechecked=True,
                exact_vertices=result['exact_vertices'], exact_edges=result['exact_edges'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'shader', 'slangc', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    exe, shader, slangc, layer, out = (p.resolve() for p in
        (args.exe, args.shader, args.slangc, args.validation_layer_dir, args.output))
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Use fresh empty output')
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs = dict(executable=exe, shader=shader, slangc=slangc,
                  validation_dll=layer / 'VkLayer_khronos_validation.dll',
                  validation_manifest=layer / 'VkLayer_khronos_validation.json',
                  harness=Path(__file__).resolve(), scanner=ROOT / 'tools/run_visible_validation.py',
                  independent_oracle=ROOT / 'tools/transform_uncertainty_oracle.py')
    for name in ('dependencies.lock.json', 'CMakeLists.txt', 'src/progressive_gpu.cpp',
                 'src/progressive_transform_tests.inl', 'src/progressive_ray_tests.inl',
                 'src/progressive_boundary_tests.inl', 'src/progressive_shared_tests.inl', 'src/instances.cpp',
                 'src/transform_uncertainty.cpp', 'include/basix/transform_uncertainty.hpp',
                 'tests/transform_uncertainty_tests.cpp', 'src/reference.cpp',
                 'src/material.cpp', 'src/material_vndf.cpp', 'src/ray_spawn.cpp',
                 'include/basix/material.hpp', 'include/basix/material_vndf.hpp',
                 'include/basix/instances.hpp', 'include/basix/reference.hpp',
                 'include/basix/progressive_gpu_abi.hpp', 'include/basix/ray_spawn.hpp',
                 'shaders/progressive_pt.slang', 'shaders/basix_material.slang',
                 'shaders/basix_material_vndf.slang', 'shaders/basix_ray_spawn.slang'):
        inputs[name] = ROOT / name
    require(shader.name == 'progressive_pt.spv' and all(p.is_file() for p in inputs.values()),
            'Supplied transform validation input missing')
    require(sha(inputs['validation_dll']) == lock['Vulkan-ValidationLayers']['dll_sha256'],
            'Validation DLL differs from pin')
    manifest = json.loads(inputs['validation_manifest'].read_text(encoding='utf-8'))['layer']
    require(manifest.get('name') == 'VK_LAYER_KHRONOS_validation' and
            manifest.get('api_version') == lock['Vulkan-ValidationLayers']['manifest_api'] and
            (layer / manifest['library_path']).resolve() == inputs['validation_dll'],
            'Validation manifest does not select supplied pinned DLL')
    version = subprocess.run([str(slangc), '-version'], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=30)
    (out / 'slang-version.log').write_text(version.stdout, encoding='utf-8')
    match = re.search(r'\b(\d+\.\d+\.\d+)\b', version.stdout)
    require(not version.returncode and match and match.group(1) == lock['Slang']['version'],
            'Slang version differs from pin')
    before = {name: sha(path) for name, path in inputs.items()}
    env = dict(os.environ, VK_LAYER_PATH=str(layer), VK_LOADER_LAYERS_DISABLE='~implicit~',
               VK_VALIDATION_VALIDATE_SYNC='1')
    cases, results, numeric = [], {}, {}

    def run(name, directory, fault='', *, trace=False):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        target = out / name
        command = [str(exe), '--validate', '--ray-transform-tests',
                   '--shader-dir', str(directory), '--output', str(target)]
        if fault:
            command += ['--transform-fault', fault]
        start = time.perf_counter()
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=600)
        elapsed = time.perf_counter() - start
        (out / (name + '.log')).write_text(process.stdout, encoding='utf-8')
        require(process.returncode == (1 if fault else 0), name + ': unexpected process exit')
        reviewed = scan(process.stdout, name, trace)
        raw = target / 'transform-ray-result.json'
        result = json.loads(raw.read_text(encoding='utf-8'))
        checked = verify_receipt(raw, require_passed=not bool(fault))
        metadata = contract(result, checked, fault)
        marker = 'FAIL: Transform ray fixture gate failed' if fault else 'PASS transform ray fixtures 96'
        require(marker in process.stdout, name + ': expected acceptance/rejection diagnostic missing')
        cases.append(dict(name=name, exit_code=process.returncode,
                          expected_outcome_verified=True, failed_probe_cases=metadata['failed_cases'],
                          numeric_readback_rechecked=True, process_elapsed_seconds=elapsed,
                          reviewed_loader_implicit_disable_messages=reviewed))
        results[name], numeric[name] = result, checked

    run('candidate', shader.parent, trace=True)
    for fault in FAULTS:
        run('fault-' + fault, shader.parent, fault)
    scratch = out / 'no-op-shader'
    scratch.mkdir()
    source = scratch / 'no-op.slang'
    source.write_text('// MIT: actually dispatched poisoned-output control.\n'
                      '[numthreads(8,8,1)] void computeMain(uint3 id:SV_DispatchThreadID) {}\n',
                      encoding='utf-8')
    target = scratch / 'progressive_pt.spv'
    compiled = subprocess.run([str(slangc), str(source), '-entry', 'computeMain', '-stage', 'compute',
        '-target', 'spirv', '-profile', 'spirv_1_5', '-emit-spirv-directly', '-fvk-use-entrypoint-name',
        '-fvk-use-c-layout', '-matrix-layout-row-major', '-fp-mode', 'precise', '-o', str(target)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=60)
    (out / 'no-op-compile.log').write_text(compiled.stdout, encoding='utf-8')
    require(not compiled.returncode and target.is_file() and sha(target) != sha(shader),
            'Distinct no-op shader compilation failed')
    no_op_hash, no_op_source_hash = sha(target), sha(source)
    # zero-budget still dispatches; omit would invalidate this shader control.
    run('compiled-no-op', scratch, 'zero-budget')
    after = {name: sha(path) for name, path in inputs.items()}
    require(before == after and sha(target) == no_op_hash and sha(source) == no_op_source_hash,
            'Frozen transform validation input changed during execution')
    receipt = dict(checked_on='2026-10-28', status='passed', harness_checks_passed=True,
        cases=cases, results=results, independent_numeric=numeric,
        input_sha256=before, input_hashes_unchanged=True,
        no_op_shader_sha256=no_op_hash, no_op_source_sha256=no_op_source_hash,
        slang_version=match.group(1), loader_confirmed_validation=True,
        synchronization_validation_requested=True,
        full_process_diagnostics='zero unreviewed backend/validation warnings/errors through teardown; exact loader implicit-disable notices counted separately',
        limits=['Fixed96-case catalog, six transforms, normal/grazing rays and separated blockers on one local GPU',
                'Representation enclosed by arithmetic; forward/object/inverse terms are conditional policies requiring normal finite intermediate operations',
                'Component altitude cap does not certify final displacement, nearby-geometry separation, arbitrary transforms or a hardware traversal bound',
                'No performance, asynchronous lifetime, temporal rendering or cross-device claim'])
    (out / 'transform-ray-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', harness_checks=len(cases), candidate_cases=96,
                         rejected_control_cases=576, actual_dispatched_shader_fault_controls=1)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
