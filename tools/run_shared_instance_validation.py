"""Pinned shared-BLAS identity/update acceptance and actual shader fault controls.

Uses supplied local tools only. Full process logs remain in --output; the
aggregate receipt separates the positive experiment from expected rejections.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import itertools
import json
import math
import os
import re
import struct
import subprocess
import sys

from run_visible_validation import scan, sha

ROOT = Path(__file__).resolve().parents[1]
EPSILON = 2.0 ** -24
EXPECTED = set(itertools.product(range(3), range(2), range(2), (-1, 1)))
IDS = (10000123, 90000007)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def vector(value, size=3):
    require(isinstance(value, list) and len(value) == size and
            all(isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x) for x in value),
            'Numeric GPU readback missing, poisoned or nonfinite')
    return value


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def subtract(a, b):
    return [x - y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def unit(value):
    length = math.sqrt(dot(value, value))
    require(length > 0 and math.isfinite(length), 'Degenerate independent normal')
    return [x / length for x in value]


def instance_catalog(phase, render=False):
    """Frozen phase matrices, retaining probe and render placement separately."""
    a = [f32(x) for x in (1, 0, 0, -1.3, 0, 1, 0, 0, 0, 0, 1, 0)]
    b = [f32(x) for x in (-1, .25, 0, 1.3, 0, 1, 0, 0, .25, 0, 1, 0)]
    if phase == 1:
        a[3], a[7], a[11] = f32(-1.15), f32(.15), f32(.1)
        b[0], b[5], b[3], b[7], b[11] = f32(-1.25), f32(.75), f32(1.2), f32(-.1), f32(-.15)
    if render:
        # C++ compound operations use uploaded FP32 coefficients.
        a[3], b[3] = f32(a[3] - f32(.7)), f32(b[3] + f32(.7))
    return [(IDS[1], b), (IDS[0], a)] if phase == 2 else [(IDS[0], a), (IDS[1], b)]


def render_clearance(image, phase):
    """Check actual serialized render placement against independent cube extrema."""
    matrices = image.get('instance_matrices')
    bounds = image.get('world_x_bounds')
    require(isinstance(matrices, list) and len(matrices) == 2 and
            isinstance(bounds, list) and len(bounds) == 2,
            'Actual rendered matrices or geometry bounds missing')
    expected_bounds = []
    observed_bounds = []
    for slot, (_, expected_matrix) in enumerate(instance_catalog(phase, True)):
        matrix = vector(matrices[slot], 12)
        require(matrix == expected_matrix, 'Actual render transform differs from the frozen clearance catalog')
        observed = vector(bounds[slot], 2)
        # All triangle-fixture vertices lie inside [-1,1]^3; its outer walls
        # include each cube extremum. Evaluate every corner independently in
        # double and apply the actual FP32 world-upload conversion.
        extrema = [f32(matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3])
                   for x, y, z in itertools.product((-1., 1.), repeat=3)]
        expected = [min(extrema), max(extrema)]
        require(observed[0] <= observed[1] and
                all(abs(a - b) <= 2e-6 for a, b in zip(observed, expected)),
                'Rendered geometry bounds disagree with independent transformed fixture extrema')
        expected_bounds.append(expected)
        observed_bounds.append(observed)
    expected_sorted, observed_sorted = sorted(expected_bounds), sorted(observed_bounds)
    expected_gap = expected_sorted[1][0] - expected_sorted[0][1]
    observed_gap = observed_sorted[1][0] - observed_sorted[0][1]
    require(expected_gap > 0 and observed_gap > 0 and abs(expected_gap - observed_gap) <= 4e-6,
            'Rendered instances overlap or lose their declared independent X clearance')
    return dict(phase=phase, independent_geometry_clearance_checked=True,
                expected_x_bounds=expected_bounds, actual_world_x_bounds=observed_bounds,
                independent_x_clearance=observed_gap)


def catalog(phase, slot, primitive):
    """Independent probe geometry, unaffected by render-only clearance shifts."""
    stable_id, matrix = instance_catalog(phase)[slot]
    triangles = (((-.6, -.6, 0), (.6, -.6, 0), (.6, .6, 0)),
                 ((-.6, -.6, 0), (.6, .6, 0), (-.6, .6, 0)))
    world = []
    for vertex in triangles[primitive]:
        p = [f32(x) for x in vertex]
        world.append([f32(matrix[r * 4] * p[0] + matrix[r * 4 + 1] * p[1] +
                          matrix[r * 4 + 2] * p[2] + matrix[r * 4 + 3]) for r in range(3)])
    target = [(world[0][i] + world[1][i] + world[2][i]) / 3 for i in range(3)]
    normal = unit(cross(subtract(world[1], world[0]), subtract(world[2], world[0])))
    if stable_id == IDS[1]:
        normal = [-x for x in normal]  # Both frozen B matrices have negative determinant.
    return stable_id, (0 if stable_id == IDS[0] else 1), target, normal


def row_outcome(row):
    """Recompute the host contract and independently verify its input catalog."""
    identity = tuple(row.get(k) for k in ('phase', 'slot', 'primitive', 'side'))
    require(identity in EXPECTED, 'Unexpected probe identity')
    phase, slot, primitive, side = identity
    stable_id, material, target, normal = catalog(phase, slot, primitive)
    stable_primitive = (1000 if stable_id == IDS[0] else 2000) + (7 if primitive == 0 else 11)
    require(row.get('epoch') == phase + 1 and row.get('expected_instance_id') == stable_id and
            row.get('expected_global') == slot * 2 + primitive and row.get('expected_material') == material and
            row.get('expected_stable_primitive') == stable_primitive,
            'Probe expected IDs/material/generation differ from the frozen catalog')
    declared_target = vector(row.get('target'))
    declared_normal = vector(row.get('expected_normal'))
    require(max(abs(x - y) for x, y in zip(target, declared_target)) <= 1e-12 and
            max(abs(x - y) for x, y in zip(normal, declared_normal)) <= 1e-12,
            'Expected probe position/normal differs from independent transformed geometry')
    origin = [f32(t + n * side * 2) for t, n in zip(target, normal)]
    direction = [f32(x) for x in unit(subtract(target, origin))]
    declared_origin, declared_direction = vector(row.get('origin')), vector(row.get('direction'))
    distance = math.sqrt(dot(subtract(target, origin), subtract(target, origin)))
    require(max(abs(x - y) for x, y in zip(origin, declared_origin)) <= 1e-12 and
            max(abs(x - y) for x, y in zip(direction, declared_direction)) <= 1e-12 and
            isinstance(row.get('expected_distance'), (int, float)) and
            math.isclose(row['expected_distance'], distance, rel_tol=1e-12, abs_tol=1e-12),
            'Probe origin/direction/distance differs from the independent catalog')
    tolerance = 16 * EPSILON * max(1, *map(abs, target))
    require(isinstance(row.get('tolerance'), (int, float)) and
            math.isclose(row['tolerance'], tolerance, rel_tol=1e-12, abs_tol=1e-15),
            'Probe tolerance differs from the frozen epsilon-scaled policy')
    require(isinstance(row.get('passed'), bool), 'Probe pass flag missing')
    try:
        hit = vector(row.get('hit'), 4)
        point = vector(row.get('point'))
        actual_normal = vector(row.get('normal'))
        spawn = vector(row.get('spawn'))
        found, front = row.get('found'), row.get('front')
        require(isinstance(found, (int, float)) and math.isfinite(found) and
                isinstance(front, (int, float)) and math.isfinite(front), 'Probe classification poisoned')
        passed = (hit[3] == 0 and found == 1 and hit[0] > 0 and
                  hit[1] == slot * 2 + primitive and hit[2] == material and
                  row.get('actual_instance_id') == stable_id and row.get('actual_primitive') == primitive and
                  row.get('actual_stable_primitive') == stable_primitive and
                  front == (1 if side == 1 else 0) and dot(actual_normal, normal) > 1 - 2e-6 and
                  max(abs(x - y) for x, y in zip(actual_normal, normal)) <= 2e-6 and
                  abs(dot(actual_normal, actual_normal) - 1) <= 8 * EPSILON and
                  abs(hit[0] - distance) <= tolerance and
                  max(abs(o + d * hit[0] - p) for o, d, p in zip(origin, direction, point)) <= tolerance and
                  max(abs(x - y) for x, y in zip(point, target)) <= tolerance and
                  dot(subtract(spawn, point), normal) * side > 0)
        # A large non-unit normal could otherwise satisfy a dot-only predicate.
        independently_valid = abs(dot(actual_normal, actual_normal) - 1) <= 8 * EPSILON
    except ValueError:
        passed = False
        independently_valid = False
    require(bool(passed) == row['passed'], 'Host probe outcome disagrees with numeric GPU readback')
    require(not passed or independently_valid, 'Passing probe returned a non-unit geometric normal')
    return bool(passed)


def contract(result, fault='', *, all_failed=False, width=16, height=16, spp=256, batch=64, transform_rays=False):
    expected = dict(fault=fault, instances_per_scene=2, blas_per_scene=1, probe_cases=24,
                    width=width, height=height, spp=spp, batch=batch, bounded_rays=True,
                    validation_enabled=True, validation_errors=0, validation_warnings=0,
                    root_bytes=208, triangle_bytes=80, transform_rays=transform_rays, instance_bytes=16, probe_blas_identity=True, probe_tlas_identity=True,
                    seed=20261028, sampler='vndf_sampled_state_v1', max_depth=8, random_bits=24,
                    next_event_estimation=True, multiple_importance_sampling=True, russian_roulette=True)
    require(all(result.get(k) == v for k, v in expected.items()),
            'Shared-instance settings, ABI or validation contract differs')
    rows = result.get('probes')
    require(isinstance(rows, list) and len(rows) == 24, 'Probe coverage differs')
    identities = [tuple(row.get(k) for k in ('phase', 'slot', 'primitive', 'side')) for row in rows]
    require(len(set(identities)) == 24 and set(identities) == EXPECTED,
            'Shared-instance Cartesian probe product differs')
    failed = sum(not row_outcome(row) for row in rows)
    require(result.get('failed_probe_cases') == failed, 'Probe failure count differs')
    require(result.get('probe_tlas_updates') == (0 if fault == 'skip-update' else 2),
            'Actual probe TLAS update count differs')
    clearance = []
    if fault:
        require(result.get('status') == 'failed' and failed > 0 and
                result.get('inflight_rejections') == 0 and result.get('inflight_record_rejections') == 0 and
                result.get('inflight_poison_rejections') == 0 and result.get('render_phases') == 0 and
                result.get('render_tlas_updates') == 0 and result.get('images') == [],
                'Fault did not reject the shared-instance experiment')
        if all_failed:
            require(failed == 24, 'Poisoned output did not reject every probe')
    else:
        require(result.get('status') == 'passed' and failed == 0 and
                result.get('inflight_rejections') == 1 and result.get('inflight_record_rejections') == 1 and
                result.get('inflight_poison_rejections') == 1 and result.get('render_phases') == 3 and
                result.get('render_tlas_updates') == 2 and result.get('render_blas_identity') is True and
                result.get('render_tlas_identity') is True,
                'Positive shared-instance lifecycle or render gate incomplete')
        images = result.get('images')
        require(isinstance(images, list) and len(images) == 3 and
                [r.get('phase') for r in images] == [0, 1, 2], 'Rendered epoch coverage differs')
        maxima = [0., 0., 0.]
        keys = ('normalized_rmse', 'low_radiance_normalized_rmse', 'mean_relative')
        for index, image in enumerate(images):
            require(image.get('epoch') == index + 1 and image.get('primary_rays') == width * height and
                    image.get('batch_reset_identity') is True, 'Rendered phase acceptance missing')
            clearance.append(render_clearance(image, index))
            for channel, (key, limit) in enumerate(zip(keys, (.05, .1, .02))):
                value = image.get(key)
                require(isinstance(value, (int, float)) and not isinstance(value, bool) and
                        math.isfinite(value) and 0 <= value <= limit, 'CPU/GPU image metric failed')
                maxima[channel] = max(maxima[channel], value)
        for key, maximum in zip(('worst_normalized_rmse', 'worst_low_radiance_rmse', 'worst_mean_relative'), maxima):
            require(isinstance(result.get(key), (int, float)) and
                    math.isclose(result[key], maximum, rel_tol=1e-12, abs_tol=1e-15),
                    'Worst image metric differs from phase measurements')
        require(isinstance(result.get('restore_hdr_byte_identity'), bool), 'Restore diagnostic missing')
    return dict(cases=24, failed_cases=failed, numeric_readback_rechecked=True,
                independent_frozen_target_normal_catalog=True,
                large_stable_id_verified=not fault and IDS[1] > 2 ** 24,
                independent_render_clearance=clearance)


def hdr_contract(target, result):
    images = []
    records = []
    width, height = result['width'], result['height']
    components = width * height * 3
    for phase in range(3):
        path = target / ('phase-' + str(phase)) / 'linear.pfm'
        data = path.read_bytes()
        header = ('PF\n' + str(width) + ' ' + str(height) + '\n-1.0\n').encode('ascii')
        require(data.startswith(header) and len(data) == len(header) + components * 4,
                'Rendered HDR dimensions/header differ')
        values = struct.unpack('<' + str(components) + 'f', data[len(header):])
        require(all(math.isfinite(x) and x >= 0 for x in values) and min(values) != max(values),
                'Rendered HDR is invalid, constant or poisoned')
        images.append(data)
        records.append(dict(phase=phase, independently_decoded_hdr_components=components, image_sha256=sha(path)))
    identity = images[0] == images[2]
    require(not result['restore_hdr_byte_identity'] or identity,
            'Claimed restored HDR identity differs from actual PFM bytes')
    return dict(phases=records, independently_decoded_restore_hdr_identity=identity,
                reported_internal_restore_identity=result['restore_hdr_byte_identity'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'shader', 'slangc', 'validation-layer-dir', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--transform-rays', action='store_true')
    args = parser.parse_args()
    exe, shader, slangc, layer, out = (p.resolve() for p in
        (args.exe, args.shader, args.slangc, args.validation_layer_dir, args.output))
    require(not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Use fresh empty output')
    out.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs = dict(executable=exe, shader=shader, slangc=slangc,
                  validation_dll=layer / 'VkLayer_khronos_validation.dll',
                  validation_manifest=layer / 'VkLayer_khronos_validation.json',
                  harness=Path(__file__).resolve(), scanner=ROOT / 'tools/run_visible_validation.py')
    for name in ('dependencies.lock.json', 'src/progressive_gpu.cpp', 'src/progressive_shared_tests.inl',
                 'src/instances.cpp', 'src/transform_uncertainty.cpp', 'include/basix/transform_uncertainty.hpp', 'src/reference.cpp', 'src/material.cpp', 'src/material_vndf.cpp',
                 'src/ray_spawn.cpp', 'include/basix/instances.hpp', 'include/basix/reference.hpp',
                 'include/basix/progressive_gpu_abi.hpp', 'include/basix/ray_spawn.hpp',
                 'shaders/progressive_pt.slang', 'shaders/basix_material.slang',
                 'shaders/basix_material_vndf.slang', 'shaders/basix_ray_spawn.slang'):
        inputs[name] = ROOT / name
    require(shader.name == 'progressive_pt.spv' and all(p.is_file() for p in inputs.values()),
            'Supplied shared-instance input missing')
    require(sha(inputs['validation_dll']) == lock['Vulkan-ValidationLayers']['dll_sha256'],
            'Validation package differs from pin')
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
    hdr_by_case = {}

    def run(name, directory, fault='', *, trace=False, all_failed=False, width=16, height=16, spp=256, batch=64):
        local = dict(env)
        local.pop('VK_LOADER_DEBUG', None)
        if trace:
            local['VK_LOADER_DEBUG'] = 'layer'
        target = out / name
        command = [str(exe), '--validate', '--shared-instances', '--bounded-rays',
                   '--width', str(width), '--height', str(height), '--spp', str(spp), '--batch', str(batch),
                   '--shader-dir', str(directory), '--output', str(target)]
        if args.transform_rays:
            command += ['--transform-rays']
        if fault:
            command += ['--shared-fault', fault]
        process = subprocess.run(command, env=local, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=600)
        (out / (name + '.log')).write_text(process.stdout, encoding='utf-8')
        require(process.returncode == (1 if fault else 0), name + ': unexpected process exit')
        reviewed = scan(process.stdout, name, trace)
        result = json.loads((target / 'shared-instance-result.json').read_text(encoding='utf-8'))
        checked = contract(result, fault, all_failed=all_failed, width=width, height=height, spp=spp, batch=batch, transform_rays=args.transform_rays)
        require(('FAIL: Shared-instance acceptance failed' if fault else 'PASS shared instances:24probes,3renderedepochs,2TLASupdatesper-scene;')
                in process.stdout, name + ': acceptance/rejection diagnostic missing')
        if not fault:
            hdr_by_case[name] = hdr_contract(target, result)
        cases.append(dict(name=name, exit_code=process.returncode, expected_outcome_verified=True,
                          failed_probe_cases=checked['failed_cases'], numeric_readback_rechecked=True,
                          reviewed_loader_implicit_disable_messages=reviewed))
        results[name] = result
        numeric[name] = checked

    run('candidate', shader.parent, trace=True)
    run('larger-preview', shader.parent, width=128, height=64, spp=16, batch=8)
    for fault in ('slot', 'epoch', 'span', 'orientation', 'omit', 'skip-update', 'distance'):
        run('fault-' + fault, shader.parent, fault, all_failed=fault in ('omit', 'distance'))
    scratch = out / 'no-op-shader'
    scratch.mkdir()
    source = scratch / 'no-op.slang'
    source.write_text('// MIT: actual dispatched poisoned-output control.\n[numthreads(8,8,1)] void computeMain(uint3 id:SV_DispatchThreadID) {}\n', encoding='utf-8')
    target = scratch / 'progressive_pt.spv'
    compiled = subprocess.run([str(slangc), str(source), '-entry', 'computeMain', '-stage', 'compute',
        '-target', 'spirv', '-profile', 'spirv_1_5', '-emit-spirv-directly', '-fvk-use-entrypoint-name',
        '-fvk-use-c-layout', '-matrix-layout-row-major', '-fp-mode', 'precise', '-o', str(target)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', timeout=60)
    (out / 'no-op-compile.log').write_text(compiled.stdout, encoding='utf-8')
    require(not compiled.returncode and target.is_file(), 'No-op shader compilation failed')
    # Nonempty fault bypasses render/held-submission assertions. Orientation does
    # not omit dispatch, unlike the separate omitted-dispatch fault.
    run('compiled-no-op', scratch, 'orientation', all_failed=True)
    after = {name: sha(path) for name, path in inputs.items()}
    require(before == after, 'Frozen shared-instance inputs changed during validation')
    receipt = dict(checked_on='2026-10-28', transform_rays=args.transform_rays, status='passed', harness_checks_passed=True,
        cases=cases, results=results, independent_numeric=numeric, hdr=hdr_by_case['candidate'], hdr_by_case=hdr_by_case,
        input_sha256=before, input_hashes_unchanged=True, no_op_shader_sha256=sha(target),
        slang_version=match.group(1), loader_confirmed_validation=True,
        synchronization_validation_requested=True,
        full_process_diagnostics='zero unreviewed backend/validation warnings/errors through teardown; exact loader implicit-disable notices counted separately',
        limits=['Two fixed shared-BLAS instances in probe and rendered scenes, three epochs and one owned queue',
                'In-flight mutation is refused and updates/retirement are synchronous; no async/multiqueue/device-loss proof',
                'Input/probe catalogs and actual render matrices/bounds independently rechecked; images cover16x16/256SPP and128x64/16SPP',
                'Empirical ray-spawn uncertainty, not a universal transform/hardware error bound or performance result'])
    (out / 'shared-instance-results.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', harness_checks=len(cases), candidate_probes=24,
                         positive_experiments=2, positive_probe_cases=48, rendered_phases=6, actual_dispatched_shader_fault_controls=1)))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, TypeError, OSError, subprocess.SubprocessError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
