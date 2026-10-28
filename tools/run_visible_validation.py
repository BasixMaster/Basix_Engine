"""Validate the distinct visible-GGX sampled-state candidate on one local GPU.

Uses supplied local tools only. Runs deterministic conformance streams,
independent distribution/energy checks, and actual shader fault controls.
Raw logs stay in --output; visible-results.json is a path-free receipt.
This does not clear the retained legacy NDF paired-transport discrepancy.
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
SEEDS = (0x467A89CF, 5411137, 11920117, 38472491)
SAMPLES = 262144
CONFIGS = (
    'lambert_normal', 'lambert_grazing', 'white_ggx_rough1_normal',
    'white_ggx_rough1_grazing', 'white_ggx_rough005_normal',
    'white_ggx_rough005_grazing', 'white_ggx_rough03_oblique',
    'white_ggx_rough005_oblique', 'white_dielectric_mixture')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def finite(value):
    """Reject JSON NaN/Infinity, including inside the numerical evidence arrays."""
    if isinstance(value, float):
        return math.isfinite(value)
    if isinstance(value, dict):
        return all(finite(v) for v in value.values())
    if isinstance(value, list):
        return all(finite(v) for v in value)
    return True


def same_fields(result, expected, label):
    require(all(result.get(k) == v for k, v in expected.items()),
            label + ': settings, identity or acceptance contract differs')


def scan(log, label, trace):
    """Scan merged output through teardown, not just installed callbacks."""
    reviewed = 0
    for line in log.splitlines():
        if re.fullmatch(r'''\[Vulkan Loader\] WARNING \| LAYER: (?:Implicit layer|Layer) "[A-Za-z0-9_]+" forced disabled because name matches filter of env var 'VK_LOADER_LAYERS_DISABLE'\.''' , line):
            reviewed += 1
            continue
        require(not re.search(r'NoGraphicsAPI validation:|Basix validation:|NoGraphicsAPI:|Validation (?:Error|Warning)|SYNC-HAZARD|\bVUID-|\b(?:ERROR|WARNING)\b', line),
                label + ': backend/validation diagnostic; inspect local log')
    if trace:
        require(re.search(r'Insert instance layer[^\n]*VK_LAYER_KHRONOS_validation', log),
                label + ': loader did not confirm validation insertion')
    return reviewed


def visible_contract(result, seed, fault=False):
    same_fields(result, dict(method='visible_ggx_v1_sampled_state', input_seed=seed,
        cases=4096, strict_cases=4095, exact_horizon_classifications=1,
        absolute_tolerance=.0003, relative_tolerance=.001,
        direction_tolerance=.00002, weight_absolute_tolerance=.00003), 'visible')
    require(result['valid'] + result['null'] == 4095, 'Visible case accounting differs')
    if fault:
        same_fields(result, dict(status='failed', failed_cases=4096), 'visible no-op')
        return
    same_fields(result, dict(status='passed', failed_cases=0, bound_failures=0), 'visible')
    for key in ('max_f_pdf_error_over_tolerance', 'max_weight_error_over_tolerance'):
        require(0 <= result[key] <= 1, 'Visible conformance tolerance exceeded')
    require(0 <= result['max_direction_or_half_error'] <= .00002,
            'Visible direction/half tolerance exceeded')
    require(result['max_bound_formula_error'] >= 0 and
            result['standalone_reconstruction_max_pdf_relative_difference'] >= 0,
            'Visible numerical diagnostic invalid')


def distribution_contract(result, fault=None):
    same_fields(result, dict(candidate='visible_normal_v1', gpu_dispatched=True,
        samples_per_config=SAMPLES, family_alpha=1e-6, weight_bound=1.0003,
        hist_float_floor=.00002, energy_float_floor=.0002,
        quadrature_envelope_limit=.00002), 'distribution')
    configs = result['configs']
    require([c['name'] for c in configs] == list(CONFIGS), 'Distribution configurations differ')
    for config in configs:
        expected, observed = config['expected_histogram'], config['observed_counts']
        require(len(expected) == len(observed) == 65 and
                all(0 <= p <= 1 for p in expected) and abs(sum(expected) - 1) < 1e-9 and
                all(isinstance(n, int) and n >= 0 for n in observed),
                'Distribution histogram schema/normalization differs')
        require(0 < config['normalized_energy_target'] <= 1.00000001 and
                len(config['normalized_energy_observed']) == 3 and
                len(config['energy_tolerances']) == 3 and
                all(t > 0 for t in config['energy_tolerances']),
                'Distribution energy schema invalid')
        for key in ('hist_refinement_envelope', 'energy_refinement_envelope'):
            require(0 <= config[key] <= .00002, 'Independent quadrature did not converge')
        bound = 4 if config['name'] == 'white_dielectric_mixture' else 1.0003
        require(config['normalized_weight_bound'] == bound,
                'Distribution weight bound differs')
        # Recompute the fixed acceptance arithmetic from serialized aggregates;
        # a host-reported status or maximum is insufficient by itself.
        log_term = math.log(2 * len(CONFIGS) * 68 / 1e-6)
        hist_ratios = []
        for p, count in zip(expected, observed):
            tolerance = (math.sqrt(2 * max(0, p * (1 - p)) * log_term / SAMPLES)
                         + 2 * log_term / (3 * SAMPLES)
                         + config['hist_refinement_envelope'] + .00002)
            hist_ratios.append(abs(count / SAMPLES - p) / tolerance)
        energy_target = config['normalized_energy_target']
        energy_tolerance = (math.sqrt(2 * max(0, energy_target * (bound - energy_target))
                                     * log_term / SAMPLES)
                            + 2 * bound * log_term / (3 * SAMPLES)
                            + config['energy_refinement_envelope'] + .0002)
        require(all(math.isclose(t, energy_tolerance, rel_tol=1e-10, abs_tol=1e-12)
                    for t in config['energy_tolerances']),
                'Distribution confidence threshold differs from fixed arithmetic')
        energy_ratios = [abs(e - energy_target) / energy_tolerance
                         for e in config['normalized_energy_observed']]
        require(sum(r > 1 for r in hist_ratios) == config['failed_hist_bins'] and
                sum(r > 1 for r in energy_ratios) == config['failed_energy_channels'] and
                math.isclose(max(hist_ratios), config['max_hist_error_over_tolerance'],
                             rel_tol=1e-8, abs_tol=1e-10) and
                math.isclose(max(energy_ratios), config['max_energy_error_over_tolerance'],
                             rel_tol=1e-8, abs_tol=1e-10),
                'Distribution serialized measurements disagree with acceptance')
        if config['name'].startswith('lambert_'):
            require(energy_target == 1 and
                    all(abs(expected[4 * b + q] - (2 * b + 1) / 1024) < 1e-14
                        for b in range(16) for q in range(4)) and expected[64] == 0,
                    'Exact Lambert anchor differs')
        if config['name'].startswith('white_ggx_rough1_'):
            z = config['uploaded_wo_z']
            require(abs(energy_target - 2 * (1 - math.log(2)) / (1 + z)) < 2e-11 and
                    all(abs(p - 1 / (64 * (1 + z))) < 2e-11 for p in expected[:64]) and
                    abs(expected[64] - z / (1 + z)) < 2e-11,
                    'Exact roughness-one anchor differs')
        if fault == 'no-op':
            require(config['invalid_outputs'] == SAMPLES,
                    'Distribution no-op did not reject every poisoned output')
            continue
        require(config['invalid_outputs'] == 0 and sum(observed) == SAMPLES,
                'Distribution invalid output or sample accounting failure')
        require(config['failed_hist_bins'] == 0 and
                0 <= config['max_hist_error_over_tolerance'] <= 1,
                'Distribution histogram failed')
        require(0 <= config['max_normalized_weight'] <= bound,
                'Distribution importance weight bound exceeded')
        if fault == 'pdf-bias':
            require(config['failed_energy_channels'] == 3 and
                    config['max_energy_error_over_tolerance'] > 1,
                    'Actual biased PDF did not fail every energy channel')
        else:
            require(config['failed_energy_channels'] == 0 and
                    0 <= config['max_energy_error_over_tolerance'] <= 1,
                    'Independent reflected energy gate failed')
    require(result['status'] == ('failed' if fault else 'passed'),
            'Distribution result status differs from expected outcome')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'distribution-exe', 'shader-dir', 'slangc',
                 'nographics-include', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    exe, distribution, directory, slangc, include, layer, out = (p.resolve() for p in
        (args.exe, args.distribution_exe, args.shader_dir, args.slangc,
         args.nographics_include, args.validation_layer_dir, args.output))
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())),
            'Use a fresh empty output directory')
    out.mkdir(parents=True, exist_ok=True)
    locks = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs = dict(visible_executable=exe, distribution_executable=distribution,
        visible_shader=directory / 'material_visible.spv',
        distribution_shader=directory / 'material_distribution.spv',
        slangc=slangc, validation_dll=layer / 'VkLayer_khronos_validation.dll',
        validation_manifest=layer / 'VkLayer_khronos_validation.json',
        nographics_shader_header=include / 'NoGraphicsAPI' / 'shader.slang')
    for name in ('material_visible.slang', 'material_distribution.slang',
                 'basix_material_vndf.slang', 'basix_material.slang'):
        inputs['source_' + name] = ROOT / 'shaders' / name
    for name in ('material_visible_gpu.cpp', 'material_distribution_gpu.cpp',
                 'material_vndf.cpp', 'material.cpp'):
        inputs['source_' + name] = ROOT / 'src' / name
    require(all(p.is_file() for p in inputs.values()), 'Supplied input or source file missing')
    require(sha(inputs['validation_dll']) == locks['Vulkan-ValidationLayers']['dll_sha256'],
            'Validation DLL differs from the pinned package')
    version = subprocess.run([str(slangc), '-version'], stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=30)
    (out / 'slang-version.log').write_text(version.stdout, encoding='utf-8')
    match = re.search(r'\b(\d+\.\d+\.\d+)\b', version.stdout)
    require(not version.returncode and match and match.group(1) == locks['Slang']['version'],
            'Slang version differs from the tested pin')
    before = {name: sha(path) for name, path in inputs.items()}
    env = dict(os.environ, VK_LAYER_PATH=str(layer), VK_LOADER_LAYERS_DISABLE='~implicit~',
               VK_VALIDATION_VALIDATE_SYNC='1')
    records, results, faults = [], {}, {}

    def run(name, program, shader_dir, expected_exit, *, seed=None, fault=None, trace=False):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        command = [str(program), '--validate', '--shader-dir', str(shader_dir),
                   '--output', str(out / name)]
        if program == exe:
            command += ['--seed', str(seed)]
        else:
            command += ['--samples', str(SAMPLES)]
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=600)
        log = process.stdout
        (out / (name + '.log')).write_text(log, encoding='utf-8')
        require(process.returncode == expected_exit, name + ': unexpected exit; inspect local log')
        reviewed = scan(log, name, trace)
        result = json.loads((out / name / 'result.json').read_text(encoding='utf-8'))
        require(finite(result), name + ': nonfinite numerical receipt')
        same_fields(result, dict(validation_enabled=True, validation_errors=0,
                                validation_warnings=0), name)
        if program == exe:
            visible_contract(result, seed, fault=fault == 'no-op')
            diagnostic = 'FAIL: Visible sampled-state conformance failed' if fault else 'PASS visible sampled-state 4096 cases;'
        else:
            distribution_contract(result, fault=fault)
            diagnostic = 'Independent GPU distribution or energy or validation gate failed' if fault else 'PASS independent GPU distribution/null/energy gate'
        require(diagnostic in log, name + ': expected outcome diagnostic missing')
        results[name] = result
        records.append(dict(name=name, exit_code=process.returncode,
            expected_outcome_verified=True, loader_trace_verified=trace,
            reviewed_loader_implicit_disable_messages=reviewed))

    def compile_control(name, source_text, output_name):
        scratch = out / (name + '-shader')
        scratch.mkdir()
        source = scratch / (name + '.slang')
        source.write_text(source_text, encoding='utf-8')
        target = scratch / output_name
        command = [str(slangc), str(source), '-entry', 'computeMain', '-stage', 'compute',
            '-target', 'spirv', '-profile', 'spirv_1_5', '-emit-spirv-directly',
            '-fvk-use-entrypoint-name', '-fvk-use-c-layout', '-matrix-layout-row-major',
            '-fp-mode', 'precise', '-capability', 'spvDescriptorHeapEXT',
            '-I', str(include), '-I', str(ROOT / 'shaders'), '-o', str(target)]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding='utf-8', errors='replace', timeout=60)
        (out / (name + '-compile.log')).write_text(process.stdout, encoding='utf-8')
        require(not process.returncode and target.is_file(), name + ': control compilation failed')
        faults[name] = dict(source_sha256=sha(source), shader_sha256=sha(target))
        return scratch

    for index, seed in enumerate(SEEDS):
        run('conformance-' + str(index), exe, directory, 0, seed=seed, trace=index == 0)
    run('distribution', distribution, directory, 0, trace=True)
    noop = '// MIT: isolated poisoned-output rejection control.\n[numthreads(64,1,1)] void computeMain(uint3 id:SV_DispatchThreadID) {}\n'
    scratch = compile_control('visible-no-op', noop, 'material_visible.spv')
    run('visible-no-op', exe, scratch, 1, seed=SEEDS[0], fault='no-op')
    scratch = compile_control('distribution-no-op', noop, 'material_distribution.spv')
    run('distribution-no-op', distribution, scratch, 1, fault='no-op')
    source = inputs['source_material_distribution.slang'].read_text(encoding='utf-8')
    assignment = 'root.results[id.x]=basixSampleVisible(c.material,c.wo,c.uLobe,c.u1,c.u2);'
    require(source.count(assignment) == 1, 'PDF-bias injection site differs; review the control')
    biased = source.replace(assignment,
        'BasixVisibleSample biased=basixSampleVisible(c.material,c.wo,c.uLobe,c.u1,c.u2);\n'
        ' if(biased.valid==1)biased.pdf*=1.1;\n root.results[id.x]=biased;')
    scratch = compile_control('distribution-pdf-bias', biased, 'material_distribution.spv')
    run('distribution-pdf-bias', distribution, scratch, 1, fault='pdf-bias')
    # Same directions and variates must leave every histogram count unchanged.
    for normal, biased_result in zip(results['distribution']['configs'],
                                     results['distribution-pdf-bias']['configs']):
        require(normal['observed_counts'] == biased_result['observed_counts'],
                'PDF-bias control changed the sampled distribution')
    after = {name: sha(path) for name, path in inputs.items()}
    require(before == after, 'Frozen input changed during validation')
    receipt = dict(status='passed', harness_checks_passed=True, cases=records,
        input_sha256=before, input_hashes_unchanged=True, fault_controls=faults,
        slang_version=match.group(1), loader_confirmed_validation=True,
        synchronization_validation_requested=True,
        full_process_diagnostics='zero unreviewed backend/validation warnings/errors including startup and teardown; exact loader implicit-disable notices counted separately',
        results=results,
        limits=['Distinct visible-GGX sampled-state contract; legacy NDF discrepancy remains unresolved',
            'One Windows GPU configuration; no cross-device claim',
            'Independent distribution/energy scope: Lambert, white conductors and one white dielectric mixture',
            'Bernstein confidence assumes iid pseudorandom variates; quadrature refinement envelopes are not rigorous interval bounds',
            'No universal material energy proof, temporal image-quality or performance claim'])
    (out / 'visible-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', harness_checks=len(records),
        conformance_cases=4096 * len(SEEDS), independent_gpu_samples=SAMPLES * len(CONFIGS),
        actual_shader_fault_controls=3)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)

