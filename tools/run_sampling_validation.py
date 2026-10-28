"""Serial sampling contracts and retained paired-transport failure validation.

Only supplied local tools are used. Raw process logs remain in the output
directory; sampling-results.json contains sanitized aggregate evidence.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'shader', 'slangc', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    exe, shader, slangc, layer, out = (p.resolve() for p in
        (args.exe, args.shader, args.slangc, args.validation_layer_dir, args.output))
    if out.exists() and any(out.iterdir()):
        raise ValueError('Use a fresh empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    locks = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    dll = layer / 'VkLayer_khronos_validation.dll'
    manifest = layer / 'VkLayer_khronos_validation.json'
    if not all(p.is_file() for p in (exe, shader, slangc, dll, manifest)):
        raise ValueError('Executable, shader, compiler or validation package missing')
    if shader.name != 'material_sampling.spv':
        raise ValueError('--shader must identify material_sampling.spv')
    if sha(dll) != locks['Vulkan-ValidationLayers']['dll_sha256']:
        raise ValueError('Validation DLL differs from the pinned package')
    version = subprocess.run([str(slangc), '-version'], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=30)
    (out / 'slang-version.log').write_text(version.stdout, encoding='utf-8')
    match = re.search(r'\b(\d+\.\d+\.\d+)\b', version.stdout)
    if version.returncode or not match or match.group(1) != locks['Slang']['version']:
        raise ValueError('Slang compiler version differs from the tested pin')
    before = {name: sha(path) for name, path in (('executable', exe), ('shader', shader), ('slangc', slangc), ('validation_dll', dll))}
    env = dict(os.environ, VK_LAYER_PATH=str(layer), VK_LOADER_LAYERS_DISABLE='~implicit~',
        VK_VALIDATION_VALIDATE_SYNC='1')
    records, results = [], {}

    def run(name, directory, expected_exit, strict=False, trace=False):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        target = out / name
        command = [str(exe), '--validate', '--shader-dir', str(directory), '--output', str(target)]
        if strict:
            command.append('--require-paired-transport')
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=180)
        log = process.stdout
        (out / (name + '.log')).write_text(log, encoding='utf-8')
        if process.returncode != expected_exit:
            raise ValueError(name + ': unexpected exit; inspect local log')
        # Scan complete merged output after process teardown, including messages
        # before our callback is installed and after it has been destroyed.
        reviewed_disabled = 0
        for line in log.splitlines():
            if re.fullmatch(r'''\[Vulkan Loader\] WARNING \| LAYER: (?:Implicit layer|Layer) "[A-Za-z0-9_]+" forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'\.''' , line):
                reviewed_disabled += 1
                continue
            if re.search(r'NoGraphicsAPI validation:|Basix validation:|NoGraphicsAPI:|Validation (?:Error|Warning)|SYNC-HAZARD|\bVUID-|\b(?:ERROR|WARNING)\b', line):
                raise ValueError(name + ': backend/validation diagnostic; inspect local log')
        if trace and not re.search(r'Insert instance layer[^\n]*VK_LAYER_KHRONOS_validation', log):
            raise ValueError('Loader did not confirm validation layer insertion')
        result = json.loads((target / 'result.json').read_text(encoding='utf-8'))
        if result.get('cases') != 4096 or not result.get('validation_enabled') or result.get('validation_errors') != 0 or result.get('validation_warnings') != 0:
            raise ValueError(name + ': incomplete coverage or validation')
        if result.get('require_paired_transport') != strict:
            raise ValueError(name + ': strict option was not applied')
        if name != 'no-op':
            expected = dict(status='partial', original_paired_sample_status='failed',
                original_paired_sample_passed=False, failed_cases=18, original_paired_failed_cases=18,
                strict_validity_cases=4095, explicit_horizon_boundary_cases=1,
                direction_and_validity_passed=True, direction_and_validity_failed_cases=0,
                returned_tuple_passed=True, returned_tuple_failed_cases=0,
                layered_contracts_passed=True, layered_failed_cases=0,
                absolute_tolerance=.0003, relative_tolerance=.001, direction_absolute_tolerance=.00002)
            if any(result.get(k) != v for k, v in expected.items()):
                raise ValueError(name + ': retained criterion or layered contract changed')
            if 'PARTIAL: layered contracts passed; original paired transport/sample criterion FAILED 18 cases;' not in log:
                raise ValueError(name + ': missing explicit retained 18-case failure')
            if strict and 'FAIL: --require-paired-transport rejects the retained original sampling discrepancy' not in log:
                raise ValueError('Strict paired failure diagnostic missing')
            for key in ('original_weight_max_relative_error', 'original_weight_max_absolute_error', 'returned_tuple_max_error_over_tolerance'):
                if not math.isfinite(result[key]) or result[key] < 0:
                    raise ValueError(name + ': invalid numerical diagnostic')
            if result['returned_tuple_max_error_over_tolerance'] > 1:
                raise ValueError(name + ': returned tuple tolerance exceeded')
        else:
            expected = dict(status='failed', original_paired_sample_status='failed',
                original_paired_sample_passed=False, original_paired_failed_cases=4096,
                failed_cases=4096, direction_and_validity_passed=False,
                direction_and_validity_failed_cases=4096, returned_tuple_passed=False,
                returned_tuple_failed_cases=4096, layered_contracts_passed=False, layered_failed_cases=4096)
            if any(result.get(k) != v for k, v in expected.items()) or 'FAIL: GPU sampling layered contracts or validation failed' not in log:
                raise ValueError('No-op poisoned-output rejection missing')
        results[name] = result
        records.append(dict(name=name, exit_code=process.returncode, expected_outcome_verified=True,
            original_paired_failed_cases=result['original_paired_failed_cases'], layered_failed_cases=result['layered_failed_cases'],
            reviewed_loader_implicit_disable_messages=reviewed_disabled))

    run('normal', shader.parent, 0, trace=True)
    run('strict', shader.parent, 1, strict=True)
    for key in results['normal']:
        if key != 'require_paired_transport' and results['normal'][key] != results['strict'][key]:
            raise ValueError('Normal/strict numerical receipts differ')
    scratch = out / 'no-op-shader'
    scratch.mkdir()
    source = scratch / 'no-op.slang'
    source.write_text('// MIT: isolated poisoned-output rejection control.\n[numthreads(64,1,1)] void computeMain(uint3 id:SV_DispatchThreadID) {}\n', encoding='utf-8')
    no_op = scratch / 'material_sampling.spv'
    compile_process = subprocess.run([str(slangc), str(source), '-entry', 'computeMain', '-stage', 'compute',
        '-target', 'spirv', '-profile', 'spirv_1_5', '-emit-spirv-directly', '-fvk-use-entrypoint-name',
        '-fvk-use-c-layout', '-matrix-layout-row-major', '-fp-mode', 'precise', '-o', str(no_op)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=60)
    (out / 'no-op-compile.log').write_text(compile_process.stdout, encoding='utf-8')
    if compile_process.returncode or not no_op.is_file():
        raise ValueError('No-op shader compilation failed')
    run('no-op', scratch, 1)
    after = {name: sha(path) for name, path in (('executable', exe), ('shader', shader), ('slangc', slangc), ('validation_dll', dll))}
    if before != after:
        raise ValueError('Executable/shader/compiler/validation DLL changed during validation')
    receipt = dict(status='partial', harness_checks_passed=True, cases=records,
        input_sha256=before, input_hashes_unchanged=True, no_op_shader_sha256=sha(no_op),
        slang_version=match.group(1), loader_confirmed_validation=True,
        synchronization_validation_requested=True, full_process_diagnostics='zero unreviewed backend/validation warnings/errors including startup and teardown; exact loader implicit-disable notices counted separately',
        results=results, limits=['Original same-variates paired transport remains failed for 18 cases; layered pass does not waive it',
            'One Windows GPU configuration; no cross-device claim', 'No independent GPU distribution/energy proof or PT image-quality acceptance'])
    (out / 'sampling-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='partial', harness_checks_passed=True, cases=len(records),
        original_paired_failed_cases=results['normal']['original_paired_failed_cases'],
        original_weight_max_relative_error=results['normal']['original_weight_max_relative_error'])))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
