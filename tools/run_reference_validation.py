"""Bounded CLI/export regressions for the original CPU reference path tracer.

Standard library only. Runs the supplied executable locally, keeps full logs
under the requested output directory, and emits a path-free summary receipt.
This does not validate a GPU renderer or constitute a performance benchmark.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import math
import struct
import subprocess
import sys
import uuid


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def ppm(path, expected):
    with path.open('rb') as file:
        if file.readline() != b'P6\n':
            raise ValueError('Preview is not the documented binary P6 format')
        dimensions = tuple(map(int, file.readline().split()))
        if dimensions != expected or file.readline() != b'255\n':
            raise ValueError('Preview dimensions or maximum value differ')
        pixels = file.read()
    if len(pixels) != expected[0] * expected[1] * 3:
        raise ValueError('Preview payload length differs')
    if len({pixels[i:i+3] for i in range(0, len(pixels), 3)}) <= 1:
        raise ValueError('Preview is spatially constant')
    return pixels


def pfm(path, expected):
    with path.open('rb') as file:
        if file.readline() != b'PF\n':
            raise ValueError('HDR output is not RGB PFM')
        dimensions = tuple(map(int, file.readline().split()))
        if dimensions != expected or file.readline() != b'-1.0\n':
            raise ValueError('HDR dimensions or documented little-endian scale differ')
        payload = file.read()
    count = expected[0] * expected[1] * 3
    if len(payload) != count * 4:
        raise ValueError('HDR payload length differs')
    values = struct.unpack('<' + str(count) + 'f', payload)
    if not all(math.isfinite(value) and value >= 0 for value in values):
        raise ValueError('HDR output contains nonfinite or negative radiance')
    if max(values) <= 0 or len(set(zip(values[0::3], values[1::3], values[2::3]))) <= 1:
        raise ValueError('HDR image is black or spatially constant')
    return values


def inspect_render(directory):
    expected = (64, 64)
    data = json.loads((directory / 'result.json').read_text(encoding='utf-8'))
    required = dict(status='passed', scene='cornell', width=64, height=64,
                    samples_per_pixel=32, seed=20261028, batch=7,
                    max_scattering_events=8, rr_start=3,
                    next_event_estimation=True, power_heuristic_mis=True,
                    russian_roulette=True)
    if any(data.get(key) != value for key, value in required.items()):
        raise ValueError('Render receipt does not match requested configuration')
    elapsed = data.get('render_elapsed_ms')
    if not isinstance(elapsed, (float, int)) or not math.isfinite(elapsed) or elapsed < 0:
        raise ValueError('Render elapsed time is invalid')
    values = pfm(directory / 'linear.pfm', expected)
    preview = ppm(directory / 'preview.ppm', expected)
    mean = sum(.2126 * r + .7152 * g + .0722 * b
               for r, g, b in zip(values[0::3], values[1::3], values[2::3])) / (64 * 64)
    if not math.isfinite(data.get('mean_linear_luminance', math.nan)) or not math.isclose(
            mean, data['mean_linear_luminance'], rel_tol=1e-6, abs_tol=1e-8):
        raise ValueError('Decoded HDR luminance disagrees with metadata')
    # PFM rows run bottom-up. PPM rows run top-down. Check the documented
    # Reinhard + standard sRGB transfer independently from the file writer.
    maximum_preview_error = 0
    for y in range(64):
        for x in range(64):
            source = ((63-y) * 64 + x) * 3
            target = (y * 64 + x) * 3
            for channel in range(3):
                linear = values[source + channel]
                mapped = linear / (1 + linear)
                srgb = 12.92 * mapped if mapped <= .0031308 else 1.055 * mapped ** (1/2.4) - .055
                encoded = math.floor(max(0, min(1, srgb)) * 255 + .5)
                maximum_preview_error = max(maximum_preview_error, abs(encoded - preview[target + channel]))
    if maximum_preview_error > 1:
        raise ValueError('Preview transfer or row order disagrees with HDR output')
    return dict(width=64, height=64, samples_per_pixel=32, seed=20261028,
                rgb_float_components=len(values), finite_nonnegative=True,
                spatially_nonconstant=True, min_linear_component=min(values),
                max_linear_component=max(values), decoded_mean_linear_luminance=mean,
                metadata_consistent=True, maximum_preview_byte_error=maximum_preview_error,
                render_elapsed_ms=elapsed,
                timing_scope='Observed local CPU render duration, not a benchmark or GPU result.',
                linear_sha256=sha256(directory / 'linear.pfm'),
                preview_sha256=sha256(directory / 'preview.ppm'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=60)
    args = parser.parse_args()
    if not 1 <= args.timeout_seconds <= 300:
        parser.error('timeout must be between 1 and 300 seconds per process')
    executable = args.executable.resolve()
    if not executable.is_file():
        parser.error('executable must be an existing file')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # New isolated run folders prevent stale receipts from satisfying a rerun.
    root = output / ('run-' + uuid.uuid4().hex[:12])
    root.mkdir()
    errors, runs = [], []

    def run(case, options, expected_exit):
        directory = root / case
        directory.mkdir()
        command = [str(executable), *options, '--output', str(directory)]
        try:
            child = subprocess.run(command, capture_output=True, text=True,
                                   encoding='utf-8', errors='replace',
                                   timeout=args.timeout_seconds)
            (directory / 'stdout.log').write_text(child.stdout, encoding='utf-8')
            (directory / 'stderr.log').write_text(child.stderr, encoding='utf-8')
            ok = child.returncode == expected_exit
            if not ok:
                errors.append(case + ': unexpected process exit')
            if expected_exit != 0 and (directory / 'result.json').exists():
                errors.append(case + ': invalid input produced a success receipt')
                ok = False
            runs.append(dict(case=case, expected_exit=expected_exit,
                             actual_exit=child.returncode, status='passed' if ok else 'failed'))
            return directory if ok else None
        except subprocess.TimeoutExpired as error:
            (directory / 'timeout.log').write_text(str(error), encoding='utf-8')
            errors.append(case + ': process timed out; owned child terminated')
            runs.append(dict(case=case, status='timeout'))
            return None

    invalid = [
        ('invalid-scene', ['--scene', 'missing']),
        ('nan-fov', ['--fov', 'NaN']),
        ('zero-spp', ['--spp', '0']),
        ('zero-width', ['--width', '0']),
        ('oversized-spp', ['--spp', '1000001']),
        ('negative-seed', ['--seed', '-1']),
        ('zero-depth', ['--depth', '0']),
        ('zero-batch', ['--batch', '0']),
    ]
    for case, options in invalid:
        run(case, options, 1)
    common = ['--width', '12', '--height', '8', '--spp', '8', '--seed', '7']
    one = run('batch-1', [*common, '--batch', '1'], 0)
    eight = run('batch-8', [*common, '--batch', '8'], 0)
    identical = False
    if one and eight:
        try:
            identical = all((one / name).read_bytes() == (eight / name).read_bytes()
                            for name in ('linear.pfm', 'preview.ppm'))
            if not identical:
                errors.append('batch-identity: PFM or PPM bytes differ')
        except (OSError, ValueError):
            errors.append('batch-identity: required output is unreadable')
    render = run('cornell-64', ['--width', '64', '--height', '64', '--spp', '32',
                              '--batch', '7', '--depth', '8', '--seed', '20261028'], 0)
    image = {}
    if render:
        try:
            image = inspect_render(render)
        except (OSError, ValueError, TypeError, KeyError, OverflowError) as error:
            # IO exceptions include absolute local names; keep those in local
            # process logs only, never the portable summary receipt.
            errors.append('render-export: ' + ('artifact IO failed' if isinstance(error, OSError) else str(error)))
    result = dict(schema_version=1, checked_on=datetime.datetime.now(datetime.timezone.utc).date().isoformat(),
                  status='failed' if errors else 'passed', executable_sha256=sha256(executable),
                  scope='Original CPU reference PT CLI, deterministic accumulation and linear/preview export correctness.',
                  not_tested=['GPU path tracing', 'importers', 'textures', 'motion guides', 'UE visual parity', 'performance acceptance'],
                  invalid_cases_rejected=sum(row.get('status') == 'passed' for row in runs[:8]),
                  batch_sizes=[1, 8], pfm_and_ppm_byte_identical=identical,
                  image=image, runs=runs, errors=errors)
    payload = json.dumps(result, indent=2, allow_nan=False) + '\n'
    (root / 'reference-validation.json').write_text(payload, encoding='utf-8')
    (output / 'reference-validation.json').write_text(payload, encoding='utf-8')
    print(json.dumps(dict(status=result['status'], runs=len(runs), invalid_cases_rejected=result['invalid_cases_rejected'],
                          pfm_and_ppm_byte_identical=identical, errors=errors)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
