"""Independent Decimal oracle for the bounded transform-ray experiment.

No engine transform, budget, intersection, or spawn implementation is imported.
The exact-real representation residual is checked separately from conditional
FP32 arithmetic and empirical traversal policies. Passing these fixed fixtures
does not establish a Vulkan implementation's undocumented traversal arithmetic.
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

U = Decimal(2) ** -24
OFFSETS = (0, 64, 512, 64, 512, 64)


def number(x):
    if isinstance(x, bool) or not isinstance(x, (int, float, Decimal)):
        raise ValueError('Missing numeric readback')
    x = Decimal.from_float(x) if isinstance(x, float) else Decimal(x)
    if not x.is_finite():
        raise ValueError('Nonfinite numeric readback')
    return x


def vector(x, n=3):
    if not isinstance(x, (list, tuple)) or len(x) != n:
        raise ValueError('Numeric vector shape differs')
    return tuple(number(v) for v in x)


def integer(x):
    x = number(x)
    if x != x.to_integral_value():
        raise ValueError('Nonintegral index')
    return int(x)


def f32(x):
    return number(struct.unpack('<f', struct.pack('<f', float(x)))[0])


def add(a, b): return tuple(x+y for x, y in zip(a, b))
def sub(a, b): return tuple(x-y for x, y in zip(a, b))
def scale(a, b): return tuple(x*b for x in a)
def dot(a, b): return sum((x*y for x, y in zip(a, b)), Decimal(0))
def norm(a): return dot(a, a).sqrt()
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def normalized(a): return scale(a, 1/norm(a))
def quantized(a): return tuple(f32(x) for x in a)
def matvec(a, v): return tuple(dot(row, v) for row in a)
def abs_matvec(a, v): return tuple(sum((abs(x)*y for x, y in zip(row, v)), Decimal(0)) for row in a)


def determinant(a):
    return dot(a[0], cross(a[1], a[2]))


def inverse(a):
    # Cofactor construction differs from the engine's double Gauss elimination.
    d = determinant(a)
    if not d:
        raise ValueError('Singular uploaded transform')
    c = (cross(a[1], a[2]), cross(a[2], a[0]), cross(a[0], a[1]))
    return tuple(tuple(c[j][i]/d for j in range(3)) for i in range(3))


def matrix_catalog(i):
    rows = ([1,0,0,0,0,1,0,0,0,0,1,0],
            [0,0,1,0,0,1,0,0,-1,0,0,0],
            [2,0,0,0,0,.5,0,0,0,0,4,0],
            [1,.25,0,0,0,1,0,0,.25,0,1,0],
            [-1,0,0,0,0,1,0,0,0,0,1,0],
            [1,0,0,.125,0,1,0,-.25,0,0,1,.5])
    if not 0 <= i < 6:
        raise ValueError('Unknown transform ID')
    m = [number(x) for x in rows[i]]
    for r in range(3):
        m[4*r+3] = f32(m[4*r+3]-m[4*r+2]*OFFSETS[i])
    return tuple(m)


def geometry_catalog(i, side, layer):
    result = []
    offset = OFFSETS[i]
    zs = [number(offset)]
    if layer:
        zs.append(f32(offset+side*.01))
    for z in zs:
        center = (Decimal(0), Decimal(0), z)
        rim = [(Decimal(x), Decimal(y), z) for x, y in ((1,0),(0,1),(-1,0),(0,-1))]
        result += [(center, rim[j], rim[(j+1)%4]) for j in range(4)]
    return result


def triangles(x):
    return [tuple(tuple(v[j:j+3]) for j in (0,3,6)) for v in (vector(t,9) for t in x)]


def transform(m, p):
    return tuple(dot(m[4*r:4*r+3], p)+m[4*r+3] for r in range(3))


def contains(p, t):
    e, f, q = sub(t[1],t[0]), sub(t[2],t[0]), sub(p,t[0])
    aa, ab, bb = dot(e,e), dot(e,f), dot(f,f)
    d = aa*bb-ab*ab
    if d <= 0:
        return False
    u, v = (bb*dot(q,e)-ab*dot(q,f))/d, (aa*dot(q,f)-ab*dot(q,e))/d
    return u >= -2*U and v >= -2*U and u+v <= 1+2*U


def required_budget(m, obj, world):
    l = tuple(m[4*r:4*r+3] for r in range(3))
    t = tuple(m[4*r+3] for r in range(3))
    inv = inverse(l)
    a = tuple(quantized(row) for row in inv)
    b = quantized(scale(matvec(inv, t), -1))
    gamma = 7*U/(1-7*U)
    exact = [transform(m,p) for p in obj]
    rep = tuple(max(abs(world[k][r]-exact[k][r]) for k in range(3))*(1+2*U) for r in range(3))
    forward = tuple(gamma*max(sum((abs(l[r][c]*p[c]) for c in range(3)),abs(t[r])) for p in obj) for r in range(3))
    extent = tuple(gamma*2*max(abs(obj[1][r]-obj[0][r]),abs(obj[2][r]-obj[0][r])) for r in range(3))
    op = abs_matvec(l,extent)
    residual = tuple(max(abs(add(matvec(a,p),b)[r]-obj[k][r]) for k,p in enumerate(exact)) for r in range(3))
    arithmetic = tuple(gamma*(x+abs(b[r])) for r,x in enumerate(abs_matvec(a,(Decimal(160),)*3)))
    ip = abs_matvec(l,add(residual,arithmetic))
    return dict(representation=rep, forward_policy=forward, object_policy=op, inverse_policy=ip)


def check_row(row):
    errors, metrics = [], {}
    def check(ok, message):
        if not ok: errors.append(message)
    try:
        with localcontext() as ctx:
            ctx.prec = 90
            i, side, target_id, layer = (integer(row[k]) for k in ('transform_id','side','target_id','layer'))
            grazing = row['grazing']
            if side not in (-1,1) or target_id not in (0,1,2) or layer not in (0,1) or not isinstance(grazing,bool) or (grazing and target_id != 2):
                raise ValueError('Fixture identity outside frozen catalog')
            m = vector(row['matrix'],12)
            check(m == matrix_catalog(i), 'Matrix differs from frozen cancellation catalog')
            obj, world = triangles(row['object_triangles']), triangles(row['world_triangles'])
            check(obj == geometry_catalog(i,side,layer), 'Object fan/layer differs from frozen catalog')
            if len(world) != len(obj) or len(obj) != (8 if layer else 4):
                raise ValueError('Object/world triangle count differs')
            for source, actual in zip(obj,world):
                check(actual == tuple(quantized(transform(m,p)) for p in source), 'Uploaded world bake differs from exact rounded catalog')
            budgets = row['budgets']
            if len(budgets) != len(obj):
                raise ValueError('Per-triangle budget count differs')
            all_budgets = []
            for source, actual, record in zip(obj,world,budgets):
                b = {key:vector(record[key]) for key in ('representation','forward_policy','object_policy','inverse_policy','total')}
                all_budgets.append(b)
                for v in b.values():
                    check(all(x >= 0 and f32(x) == x for x in v), 'Budget is negative or differs from uploaded FP32')
                required = required_budget(m,source,actual)
                for key, v in required.items():
                    check(all(x >= y for x,y in zip(b[key],v)), key+' does not enclose independent Decimal lower bound')
                check(all(b['total'][r] >= sum((b[k][r] for k in required),Decimal(0)) for r in range(3)), 'Total budget is below sum of serialized components')
                edges = [sub(actual[1],actual[0]),sub(actual[2],actual[0]),sub(actual[2],actual[1])]
                altitude = norm(cross(edges[0],edges[1]))/max(norm(e) for e in edges)
                check(max(b['total']) <= altitude/64, 'Extra budget exceeds altitude component policy')
            l = tuple(m[4*r:4*r+3] for r in range(3))
            local_target = ((Decimal(0),Decimal(0)),(Decimal('.5'),Decimal(0)),(Decimal('.25'),Decimal('.125')))[target_id]+(Decimal(OFFSETS[i]),)
            target = quantized(transform(m,local_target))
            declared_d = quantized(normalized(matvec(l,(Decimal(1 if grazing else 0),Decimal(0),Decimal(side)*(Decimal(2)**-10 if grazing else 1)))))
            declared_secondary = quantized(normalized(matvec(l,(Decimal('.125'),Decimal('.0625'),Decimal(side)))))
            declared_origin = quantized(sub(target,scale(declared_d,Decimal(10 if grazing else 2))))
            origin, direction, secondary = (vector(row[k]) for k in ('primary_origin','primary_direction','secondary_direction'))
            check(vector(row['target_world']) == target, 'Target differs from frozen catalog')
            check(direction == declared_d and secondary == declared_secondary, 'Direction differs from frozen normalized catalog')
            check(origin == declared_origin, 'Primary origin differs from frozen catalog')
            check(max(abs(x) for x in origin) <= 32 and all(abs(x) <= 1 for x in direction+secondary), 'Ray outside declared domain')
            check(abs(norm(direction)-1) <= 8*U and abs(norm(secondary)-1) <= 8*U, 'Ray direction is not unit length')
            first, second = vector(row['first'],4), vector(row['second'],4)
            check(row['first_found'] is True and number(row['hit_error']) == 0, 'Primary missing or errored')
            index = integer(first[1])
            if not 0 <= index < 4:
                raise ValueError('Primary index outside source fan')
            check(0 < first[0] <= 128, 'Primary distance outside domain')
            u,v = first[2:]
            check(0 <= u <= 1 and 0 <= v <= 1 and f32(u+v) <= 1, 'Primary barycentrics outside FP32 simplex')
            t = world[index]
            point, spawn, normal, error, gpu = (vector(row[k]) for k in ('point','spawn','normal','point_error','gpu_budget'))
            check(gpu == all_budgets[index]['total'], 'GPU budget differs from selected uploaded triangle')
            check(all(x >= y for x,y in zip(error,gpu)), 'Point envelope below selected transform budget')
            # A separately derived upper envelope prevents a corrupt readback
            # from granting itself an arbitrarily large geometric tolerance.
            # 64u covers the finite sequence of float roundings/outward steps;
            # the absolute term covers next-float steps at exact zero.
            gamma = 7*U/(1-7*U)
            extent = max(abs(t[1][r]-t[0][r])+abs(t[2][r]-t[0][r]) for r in range(3))
            envelope_limit = tuple((1+64*U)*(gpu[r]+gamma*(extent+abs(t[0][r])+abs(u)*(abs(t[1][r])+abs(t[0][r]))+abs(v)*(abs(t[2][r])+abs(t[0][r]))))+64*Decimal(2)**-149 for r in range(3))
            check(all(0 <= e <= cap for e,cap in zip(error,envelope_limit)), 'Point error exceeds independently derived float envelope cap')
            geometric_normal = normalized(cross(sub(t[1],t[0]),sub(t[2],t[0])))
            authored_normal = scale(geometric_normal,1 if determinant(l) > 0 else -1)
            check(abs(norm(normal)-1) <= 8*U and dot(normal,authored_normal) >= 1-16*U, 'Normal differs from authored geometric hemisphere or unit length')
            bary = add(t[0],add(scale(sub(t[1],t[0]),u),scale(sub(t[2],t[0]),v)))
            object_bary = add(obj[index][0],add(scale(sub(obj[index][1],obj[index][0]),u),scale(sub(obj[index][2],obj[index][0]),v)))
            exact_forward = transform(m,object_bary)
            ray_point = add(origin,scale(direction,first[0]))
            reconstruction = tuple(8*U*max(Decimal(1),abs(x)) for x in bary)
            check(all(abs(x-y) <= e for x,y,e in zip(point,bary,reconstruction)), 'Point differs from FP32 barycentric reconstruction allowance')
            check(all(abs(x-y) <= e for x,y,e in zip(point,exact_forward,error)), 'Exact transformed barycentric point outside reported error box')
            check(all(abs(x-y) <= e+r for x,y,e,r in zip(point,ray_point,error,reconstruction)), 'Primary distance ray equation disagrees with reconstructed point')
            check(contains(target,t), 'Primary tie does not contain frozen boundary target')
            # Quantizing a nearly parallel origin/direction can move the actual
            # plane intersection tangentially away from the ideal target. Check
            # its exact ray/plane equation instead of hiding that amplification
            # behind an arbitrary Cartesian target tolerance.
            incidence = dot(direction,geometric_normal)
            primary_plane_t = dot(sub(t[0],origin),geometric_normal)/incidence
            plane_tolerance = dot(tuple(abs(x) for x in geometric_normal),add(error,reconstruction))
            check(abs(first[0]-primary_plane_t)*abs(incidence) <= plane_tolerance, 'Primary distance differs from independent plane intersection')
            sign = Decimal(1) if dot(geometric_normal,secondary)>0 else Decimal(-1)
            separation = dot(sub(spawn,t[0]),geometric_normal)*sign
            check(separation > 0, 'Spawn remains behind or on outgoing source plane')
            projected = dot(tuple(abs(x) for x in normal),error)/dot(normal,normal)
            movement_cap = tuple(abs(normal[r])*projected*(1+64*U)+4*U*max(Decimal(1),abs(point[r]),abs(spawn[r])) for r in range(3))
            check(all(abs(x-y) <= cap for x,y,cap in zip(spawn,point,movement_cap)), 'Spawn movement exceeds projected error and float rounding cap')
            check(max(abs(x) for x in spawn) <= 32, 'Spawn outside bounded origin domain')
            check(second[3] == 0, 'Secondary trace error')
            if layer:
                si = integer(second[1])
                if second[2] != 1 or not 4 <= si < 8 or not 0 < second[0] <= 128:
                    raise ValueError('Blocker missing, self-hit, or invalid distance/index')
                blocker = world[si]
                gap = dot(sub(blocker[0],t[0]),geometric_normal)*sign
                check(0 < separation < gap, 'Spawn crossed the uploaded blocker plane')
                q = add(spawn,scale(secondary,second[0]))
                tolerance = dot(tuple(abs(x) for x in geometric_normal),add(error,reconstruction))
                expected_t = dot(sub(blocker[0],spawn),geometric_normal)/dot(secondary,geometric_normal)
                check(abs(second[0]-expected_t)*abs(dot(secondary,geometric_normal)) <= tolerance, 'Secondary distance differs from independent plane intersection')
                check(abs(dot(sub(q,blocker[0]),geometric_normal)) <= tolerance and contains(q,blocker), 'Secondary ray equation misses returned blocker triangle')
            else:
                check(second[2] == 0, 'Source-only secondary falsely intersects')
            metrics = dict(spawn_separation=float(separation), maximum_budget=float(max(gpu)), exact_vertex=(u,v) in ((0,0),(1,0),(0,1)), exact_edge=(u,v) not in ((0,0),(1,0),(0,1)) and (u==0 or v==0 or f32(u+v)==1))
    except (KeyError,ValueError,TypeError,ArithmeticError,OverflowError) as e:
        errors.append(str(e))
    return dict(passed=not errors,errors=list(dict.fromkeys(errors)),metrics=metrics)


def validate_result(result, require_passed=True):
    rows = result['fixtures']
    identities = [(r['transform_id'],r['side'],r['target_id'],r['grazing'],r['layer']) for r in rows]
    expected = {(i,s,t,g,l) for i,s,t,g,l in itertools.product(range(6),(-1,1),range(3),(False,True),(0,1)) if not g or t==2}
    if len(identities) != 96 or len(set(identities)) != 96 or set(identities) != expected:
        raise ValueError('Frozen 96-case Cartesian catalog differs')
    if result.get('origin_limit') != 32 or result.get('max_distance') != 128 or result.get('root_bytes') != 208 or result.get('triangle_bytes') != 80:
        raise ValueError('Domain or shader ABI receipt differs')
    diagnostics = [check_row(r) for r in rows]
    failures = sum(not d['passed'] for d in diagnostics)
    # Host booleans are checked only as accounting; they never establish success.
    disagreements = [j for j,(r,d) in enumerate(zip(rows,diagnostics)) if not isinstance(r.get('passed'),bool) or r['passed'] != d['passed']]
    if require_passed and (failures or disagreements):
        bad = next((j for j,d in enumerate(diagnostics) if not d['passed']),disagreements[0] if disagreements else 0)
        raise ValueError('Independent transform oracle failed: '+str(identities[bad])+' '+ '; '.join(diagnostics[bad]['errors']))
    if result.get('cases') != 96 or result.get('failed_cases') != sum(r.get('passed') is False for r in rows):
        raise ValueError('Host receipt accounting differs')
    vertices = sum(d['metrics'].get('exact_vertex',False) for d in diagnostics)
    edges = sum(d['metrics'].get('exact_edge',False) for d in diagnostics)
    if require_passed and (result.get('status') != 'passed' or result.get('failed_cases') != 0 or result.get('exact_vertices') != vertices or result.get('exact_edges') != edges):
        raise ValueError('Receipt status or boundary observation accounting differs')
    return dict(cases=96,failed_cases=failures,actual_vertices=vertices,actual_edges=edges,observed_exact_vertices=vertices,observed_exact_edges=edges,host_disagreements=disagreements,numeric_readback_rechecked=True,arithmetic='Decimal precision90; exact dyadic catalog inverse; FP32 uploaded inputs',limits='Fixed bounded experiment; conditional arithmetic proxies and empirical traversal policies do not prove Vulkan hardware behavior',diagnostics=diagnostics)


def verify_receipt(path, require_passed=True):
    return validate_result(json.loads(Path(path).read_text(encoding='utf-8')),require_passed)


def self_test(result):
    """Poison real positive readback; each independent rejection must fire."""
    validate_result(result)
    base = next(r for r in result['fixtures'] if r['transform_id']==0 and r['side']==1 and r['target_id']==2 and not r['grazing'] and r['layer']==1)
    mutations = [
        ('matrix',lambda r:r['matrix'].__setitem__(0,2)),
        ('object',lambda r:r['object_triangles'][0].__setitem__(0,.125)),
        ('world',lambda r:r['world_triangles'][0].__setitem__(0,.125)),
        ('forward budget',lambda r:r['budgets'][0]['forward_policy'].__setitem__(0,0)),
        ('object budget',lambda r:r['budgets'][0]['object_policy'].__setitem__(0,0)),
        ('inverse budget',lambda r:r['budgets'][0]['inverse_policy'].__setitem__(0,0)),
        ('total sum',lambda r:r['budgets'][0]['total'].__setitem__(0,0)),
        ('GPU mapping',lambda r:r['gpu_budget'].__setitem__(0,0)),
        ('inflated envelope',lambda r:r['point_error'].__setitem__(0,1)),
        ('ray distance',lambda r:r['first'].__setitem__(0,r['first'][0]+1)),
        ('outside barycentric',lambda r:r['first'].__setitem__(2,-.01)),
        ('point',lambda r:r['point'].__setitem__(0,r['point'][0]+.01)),
        ('normal',lambda r:r['normal'].__setitem__(2,-1)),
        ('spawn side',lambda r:r['spawn'].__setitem__(2,-.001)),
        ('spawn jump',lambda r:r['spawn'].__setitem__(0,r['spawn'][0]+.01)),
        ('blocker skip',lambda r:r['spawn'].__setitem__(2,.02)),
        ('blocker missing',lambda r:r['second'].__setitem__(2,0)),
        ('blocker distance',lambda r:r['second'].__setitem__(0,r['second'][0]+.01)),
        ('blocker index',lambda r:r['second'].__setitem__(1,7)),
        ('origin domain',lambda r:r['primary_origin'].__setitem__(0,40)),
        ('direction domain',lambda r:r['primary_direction'].__setitem__(2,2)),
    ]
    for label, mutate in mutations:
        row = copy.deepcopy(base)
        mutate(row)
        if check_row(row)['passed']:
            raise ValueError('Independent oracle failed to reject mutation: '+label)
    return dict(positive_cases=96, malformed_rejections=len(mutations))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('receipt',nargs='?',type=Path)
    p.add_argument('--receipt',dest='receipt_option',type=Path)
    p.add_argument('--output',type=Path)
    p.add_argument('--allow-failures',action='store_true')
    p.add_argument('--self-test',action='store_true',help='Validate supplied positive receipt, then reject numeric mutations')
    args = p.parse_args()
    source = args.receipt_option or args.receipt
    if source is None: p.error('receipt is required')
    if args.self_test:
        print(json.dumps(self_test(json.loads(source.read_text(encoding='utf-8')))))
        return
    result = verify_receipt(source,not args.allow_failures)
    if args.output: args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='diagnostics'}))


if __name__ == '__main__':
    try: main()
    except (ValueError,KeyError,OSError,ArithmeticError) as e:
        print(str(e),file=sys.stderr)
        raise SystemExit(1)
