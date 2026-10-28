"""Independent high-precision oracle for bounded Vulkan boundary/instance fixtures.

This module never calls the engine's material, spawn, or transformation helpers.
It checks serialized GPU readback against Decimal triangle/plane mathematics.
Declared FP32-scale fixture tolerances are acceptance limits, not a hardware
error theorem. Shared-edge primitive ties are accepted only when the requested
point belongs to the returned uploaded triangle within the declared boundary
membership tolerance. No arbitrary-transform or cross-device claim is made.
"""
# SPDX-License-Identifier: MIT
from decimal import Decimal, localcontext
from pathlib import Path
import argparse
import copy
import itertools
import json
import math
import struct
import sys

EPSILON = Decimal(2) ** -24
POSITION_ULPS = 16
MEMBERSHIP_TOLERANCE = 2 * EPSILON
TRANSFORM_ULPS = 8


def _number(value):
    if isinstance(value, Decimal):
        if not value.is_finite():raise ValueError('Numeric readback missing or nonfinite')
        return value
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Numeric readback missing or nonfinite')
    return Decimal.from_float(value) if isinstance(value, float) else Decimal(value)


def _vector(value, length=3):
    if not isinstance(value, list) or len(value) != length:
        raise ValueError('Numeric vector shape differs')
    return tuple(_number(x) for x in value)


def _integer(value, label):
    if isinstance(value, bool):return int(value)
    n = _number(value)
    if n != n.to_integral_value():
        raise ValueError(label + ' must be an integer')
    return int(n)


def _f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', float(value)))[0]
    except (OverflowError, struct.error):
        return math.inf


def _add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def _sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def _mul(a, b):
    return tuple(x * b for x in a)


def _dot(a, b):
    return sum((x * y for x, y in zip(a, b)), Decimal(0))


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])


def _norm(a):
    return _dot(a, a).sqrt()


def _max_abs(a):
    return max(abs(x) for x in a)


def _triangles(value):
    if not isinstance(value, list) or len(value) not in (4, 8):
        raise ValueError('Expected four source fan triangles, optionally four blockers')
    return [tuple(tuple(v[i:i + 3]) for i in range(0, 9, 3))
            for v in (_vector(row, 9) for row in value)]


def _barycentric(point, triangle):
    a, b, c = triangle
    e1, e2, delta = _sub(b, a), _sub(c, a), _sub(point, a)
    aa, ab, bb = _dot(e1, e1), _dot(e1, e2), _dot(e2, e2)
    denominator = aa * bb - ab * ab
    if denominator <= 0:
        raise ValueError('Uploaded triangle is degenerate')
    da, db = _dot(delta, e1), _dot(delta, e2)
    return ((bb * da - ab * db) / denominator,
            (aa * db - ab * da) / denominator)


def _contains(point, triangle):
    u, v = _barycentric(point, triangle)
    return u >= -MEMBERSHIP_TOLERANCE and v >= -MEMBERSHIP_TOLERANCE and u + v <= 1 + MEMBERSHIP_TOLERANCE


def _transform(matrix, point):
    return tuple(sum((matrix[r * 4 + c] * point[c] for c in range(3)), matrix[r * 4 + 3])
                 for r in range(3))


def matrix_catalog(transform_id, scale):
    """Frozen declared input families, independently checked against receipts."""
    matrices = [
        [1,0,0,0, 0,1,0,0, 0,0,1,0],
        [0,0,1,0, 0,1,0,0, -1,0,0,0],
        [2,0,0,0, 0,.5,0,0, 0,0,4,0],
        [1,.25,0,0, 0,1,0,0, .25,0,1,0],
        [-1,0,0,0, 0,1,0,0, 0,0,1,0],
        [1,0,0,_f32(.125*float(scale)), 0,1,0,_f32(-.25*float(scale)), 0,0,1,_f32(.5*float(scale))],
    ]
    if not 0 <= transform_id < len(matrices):raise ValueError('Unknown transform family')
    return tuple(_number(x) for x in matrices[transform_id])


def target_catalog(target_id, scale):
    values = [(0,0,0),(.5,0,0),(.25,.125,0),(.5,2**-18,0),(.5,-2**-18,0)]
    if not 0 <= target_id < len(values):raise ValueError('Unknown target family')
    return tuple(_number(_f32(float(scale)*x)) for x in values[target_id])


def object_catalog(scale, side, layer):
    triangles=[]
    for z in ([0.,_f32(side*1e-5*float(scale))] if layer else [0.]):
        center=(0.,0.,z);s=_f32(scale);rim=[(s,0.,z),(0.,s,z),(-s,0.,z),(0.,-s,z)]
        for i in range(4):triangles.append(tuple(tuple(_number(x) for x in p) for p in (center,rim[i],rim[(i+1)%4])))
    return triangles


def boundary_observations(row):
    """Count actual finite barycentric observations, independent of pass/fail."""
    try:
        _vector(row['first'],4);_vector(row['point']);_vector(row['normal']);_vector(row['spawn']);_vector(row['second'],4);_number(row['hit_error'])
        if _integer(row['first_found'],'Primary found')!=1:return False,False
        u,v=map(float,row['first'][2:])
        vertex=(u,v) in ((0.,0.),(1.,0.),(0.,1.))
        edge=not vertex and (u==0 or v==0 or _f32(u+v)==1)
        return vertex,edge
    except (KeyError,TypeError,ValueError,ArithmeticError):return False,False


def check_row(row):
    """Return independent outcome/diagnostics; never trust the host passed flag.

    Invalid or poisoned numeric payloads produce a failed row. Structural and
    numeric faults are deliberately usable by expected-negative GPU controls.
    validate_result additionally checks accounting and host flag agreement.
    """
    errors = []
    metrics = {}
    try:
        with localcontext() as context:
            context.prec = 60
            scale = _number(row['scale'])
            side = _integer(row['side'], 'Side')
            layer = _integer(row['expected_layer'], 'Expected layer')
            if scale <= 0 or side not in (-1, 1) or layer not in (0, 1):
                raise ValueError('Fixture scale, side or layer differs')
            matrix = _vector(row['matrix'], 12)
            transform_id=_integer(row['transform_id'],'Transform ID')
            target_id=_integer(row['target_id'],'Target ID')
            declared_matrix=matrix_catalog(transform_id,scale)
            if any(_f32(a)!=_f32(b) for a,b in zip(matrix,declared_matrix)):
                errors.append('Transform matrix differs from the frozen input family')
            objects = _triangles(row['object_triangles'])
            world = _triangles(row['world_triangles'])
            declared_objects=object_catalog(scale,side,layer)
            if objects != declared_objects:errors.append('Object geometry differs from the frozen fan/layer input')
            if len(objects) != len(world) or len(world) != (8 if layer else 4):
                raise ValueError('Object/world geometry or layer count differs')
            origin = _vector(row['primary_origin'])
            direction = _vector(row['primary_direction'])
            secondary = _vector(row['secondary_direction'])
            target = _vector(row['target_world'])
            declared_target=_transform(declared_matrix,target_catalog(target_id,scale))
            if any(_f32(a)!=_f32(b) for a,b in zip(target,declared_target)):
                errors.append('Requested target differs from the frozen boundary input')
            first = _vector(row['first'], 4)
            second = _vector(row['second'], 4)
            point, spawn = _vector(row['point']), _vector(row['spawn'])
            read_normal = _vector(row['normal'])
            if 'helper_valid' in row and _integer(row['helper_valid'], 'Spawn helper valid') != 1:
                errors.append('Spawn helper rejected the primary point')
            if _integer(row['hit_error'], 'Primary error') != 0 or _integer(row['first_found'], 'Primary found') != 1:
                errors.append('Primary intersection missing or invalid')
            magnitude = max([scale, _max_abs(origin)] + [_max_abs(v) for t in world for v in t])
            tolerance = POSITION_ULPS * EPSILON * magnitude
            metrics['position_tolerance'] = float(tolerance)
            # Matrix equations are evaluated at higher precision. The envelope
            # allows the separately uploaded FP32 world vertices to round; no
            # engine transformation routine participates in this comparison.
            for object_triangle, world_triangle in zip(objects, world):
                for local, actual in zip(object_triangle, world_triangle):
                    expected = _transform(matrix, local)
                    for axis in range(3):
                        absolute_sum = sum((abs(matrix[axis * 4 + c] * local[c]) for c in range(3)), abs(matrix[axis * 4 + 3]))
                        transform_tolerance = TRANSFORM_ULPS * EPSILON * max(absolute_sum, scale)
                        if abs(expected[axis] - actual[axis]) > transform_tolerance:
                            errors.append('Object-to-world uploaded vertex mismatch')
                            break
            index = _integer(first[1], 'Primary index')
            if not 0 <= index < 4:
                raise ValueError('Primary hit is outside the source fan')
            if first[0] <= 0:
                errors.append('Primary distance is nonpositive')
            u, v = first[2:]
            # FP32 addition is part of the serialized shader domain contract.
            # A rounded sum of one is permitted, but no extra epsilon admits
            # arbitrary outside barycentrics. Independent membership follows.
            if not (0 <= u <= 1 and 0 <= v <= 1 and _f32(u + v) <= 1):
                errors.append('Hardware barycentrics violate the FP32 domain')
            triangle = world[index]
            a, b, c = triangle
            e1, e2 = _sub(b, a), _sub(c, a)
            normal_raw = _cross(e1, e2)
            normal_length = _norm(normal_raw)
            if normal_length <= 0:
                raise ValueError('Primary uploaded plane is degenerate')
            normal = _mul(normal_raw, 1 / normal_length)
            if abs(_norm(read_normal) - 1) > 8 * EPSILON or _dot(read_normal, normal) < 1 - 16 * EPSILON:
                errors.append('Geometric normal disagrees with the uploaded world triangle')
            if abs(_norm(direction) - 1) > 8 * EPSILON or abs(_norm(secondary) - 1) > 8 * EPSILON:
                errors.append('Ray directions are not normalized')
            bary_point = _add(a, _add(_mul(e1, u), _mul(e2, v)))
            ray_point = _add(origin, _mul(direction, first[0]))
            point_error = max(_max_abs(_sub(point, bary_point)), _max_abs(_sub(point, ray_point)), _max_abs(_sub(point, target)))
            metrics['maximum_point_error'] = float(point_error)
            if point_error > tolerance:
                errors.append('Primary barycentric/ray/target position mismatch')
            if not _contains(target, triangle):
                errors.append('Primitive tie is away from the requested shared boundary')
            if abs(_dot(_sub(target, a), normal)) > tolerance:
                errors.append('Requested target is away from the primary plane')
            orientation = _dot(normal, secondary)
            if orientation == 0:
                errors.append('Secondary ray is tangent to the source plane')
            signed_spawn = _dot(_sub(spawn, a), normal) * (1 if orientation > 0 else -1)
            metrics['oriented_spawn_separation'] = float(signed_spawn)
            if signed_spawn <= 0:
                errors.append('Spawn did not leave the source plane on the outgoing side')
            if _integer(second[3], 'Secondary error') != 0:
                errors.append('Secondary intersection is invalid')
            found = _integer(second[2], 'Secondary found')
            if layer == 0:
                if found != 0:
                    errors.append('Source-only secondary ray falsely reintersected')
            else:
                secondary_index = _integer(second[1], 'Secondary index')
                if found != 1 or not 4 <= secondary_index < 8 or second[0] <= 0:
                    errors.append('Nearby blocker was missed or replaced by a self-hit')
                else:
                    blocker = world[secondary_index]
                    ba, bb, bc = blocker
                    bn = _cross(_sub(bb, ba), _sub(bc, ba))
                    bn_length = _norm(bn)
                    if bn_length <= 0:
                        raise ValueError('Blocker plane is degenerate')
                    bn = _mul(bn, 1 / bn_length)
                    separation = abs(_dot(_sub(ba, a), normal))
                    metrics['actual_plane_separation'] = float(separation)
                    if 'actual_gap' in row and abs(_number(row['actual_gap']) - separation) > max(separation * Decimal('0.000001'), scale * Decimal('1e-12')):
                        errors.append('Serialized uploaded gap disagrees with the independent plane separation')
                    if separation <= 0 or signed_spawn >= separation:
                        errors.append('Spawn crossed the actual uploaded blocker plane')
                    second_point = _add(spawn, _mul(secondary, second[0]))
                    if abs(_dot(_sub(second_point, ba), bn)) > tolerance or not _contains(second_point, blocker):
                        errors.append('Secondary distance/index does not intersect the uploaded blocker')
    except (KeyError, TypeError, ValueError, ArithmeticError, OverflowError) as error:
        errors.append(str(error))
    return dict(passed=not errors, errors=list(dict.fromkeys(errors)), metrics=metrics)


def validate_result(result, expected_identities=None, require_passed=True, verify_host_flags=True):
    """Check all rows and accounting, optionally a frozen Cartesian identity set.

    Returns numeric diagnostics; raises on receipt-accounting disagreement or a
    required passing gate. Expected-negative callers use require_passed=False
    and separately require the intended failures. This never declares a GPU
    control successful merely because the host labels it failed.
    """
    rows = result['fixtures']
    if not isinstance(rows, list) or not rows:
        raise ValueError('Boundary fixture rows missing')
    identities, diagnostics = [], []
    for row in rows:
        identity = tuple(row[k] for k in ('scale', 'transform_id', 'side', 'target_id', 'expected_layer'))
        identities.append(identity)
        diagnostic = check_row(row)
        diagnostics.append(diagnostic)
        if verify_host_flags and (not isinstance(row.get('passed'), bool) or row['passed'] != diagnostic['passed']):
            raise ValueError('Host fixture outcome disagrees with independent numeric oracle: ' + str(identity) + ' ' + '; '.join(diagnostic['errors']))
    if len(set(identities)) != len(identities):
        raise ValueError('Boundary fixture identity duplicated')
    if expected_identities is not None and set(identities) != set(expected_identities):
        raise ValueError('Boundary Cartesian product differs')
    failed = sum(not row['passed'] for row in diagnostics)
    observed_vertices = observed_edges = 0
    for row in rows:
        vertex,edge=boundary_observations(row)
        observed_vertices+=vertex;observed_edges+=edge
        if 'exact_vertex_hit' in row and row['exact_vertex_hit']!=vertex:raise ValueError('Host exact-vertex flag differs')
        if 'exact_edge_hit' in row and row['exact_edge_hit']!=edge:raise ValueError('Host exact-edge flag differs')
    if result.get('cases') != len(rows) or result.get('failed_cases') != failed:
        raise ValueError('Boundary receipt accounting differs from numeric readback')
    if result.get('status') != ('failed' if failed else 'passed'):
        raise ValueError('Boundary receipt status differs from numeric readback')
    if require_passed and failed:
        raise ValueError('Independent boundary oracle failed ' + str(failed) + ' fixtures')
    return dict(cases=len(rows), failed_cases=failed, numeric_readback_rechecked=True,
                observed_exact_vertices=observed_vertices, observed_exact_edges=observed_edges,
                tolerance_policy=dict(position_ulps=POSITION_ULPS, membership=float(MEMBERSHIP_TOLERANCE),
                                      transform_ulps=TRANSFORM_ULPS, arithmetic='Decimal precision60; FP32-domain sum check'),
                diagnostics=diagnostics)


def _synthetic_row(layer=0):
    fan = [[0, 0, 0, 1, 0, 0, 0, 1, 0], [0, 0, 0, 0, 1, 0, -1, 0, 0],
           [0, 0, 0, -1, 0, 0, 0, -1, 0], [0, 0, 0, 0, -1, 0, 1, 0, 0]]
    if layer:
        gap = _f32(1e-5)
        fan += [[v + (gap if i % 3 == 2 else 0) for i, v in enumerate(t)] for t in fan[:4]]
    return dict(scale=1, transform_id=0, side=1, target_id=2, expected_layer=layer,
                matrix=[1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0], object_triangles=copy.deepcopy(fan),
                world_triangles=fan, primary_origin=[.25, .125, 1], primary_direction=[0, 0, -1],
                secondary_direction=[0, 0, 1], target_world=[.25, .125, 0], first=[1, 0, .25, .125],
                point=[.25, .125, 0], normal=[0,0,1], spawn=[.25, .125, 1e-6],
                second=[9e-6, 4, 1, 0] if layer else [0, 0, 0, 0], first_found=1, hit_error=0, passed=True)


def self_test():
    for layer in (0, 1):
        row = _synthetic_row(layer)
        assert check_row(row)['passed'], check_row(row)
    mutations = [lambda r: r['first'].__setitem__(1, 3),
                 lambda r: r['first'].__setitem__(2, -1e-4),
                 lambda r: r['first'].__setitem__(3, 1.1),
                 lambda r: r['point'].__setitem__(2, .01),
                 lambda r: r['spawn'].__setitem__(2, -1e-6),
                 lambda r: r['world_triangles'][0].__setitem__(3, 2),
                 lambda r: r['second'].__setitem__(2, 1),
                 lambda r: r['first'].__setitem__(0, None)]
    for mutate in mutations:
        row = _synthetic_row()
        mutate(row)
        assert not check_row(row)['passed'], row
    row = _synthetic_row(1)
    row['spawn'][2] = 2e-5
    assert not check_row(row)['passed']
    print(json.dumps(dict(status='passed', synthetic_positive=2, malformed_rejections=9)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', nargs='?', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.receipt:
        parser.error('receipt is required unless --self-test is selected')
    result = json.loads(args.receipt.read_text(encoding='utf-8'))
    summary = validate_result(result)
    if args.output:
        args.output.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(status='passed', cases=summary['cases'], failed_cases=summary['failed_cases'])))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, ArithmeticError) as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
