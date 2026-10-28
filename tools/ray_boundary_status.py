"""Receipt-driven hardware boundary and fixed instance-catalog evidence."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import json
import math

DOC = Path(__file__).resolve().parents[1] / 'docs'


def _receipt(name):
    path = DOC / name
    if not path.exists():
        return None
    value = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise ValueError('Expected selected receipt object: ' + name)
    return value


def _scene_results(receipt):
    values = receipt.get('results', {}) if receipt else {}
    if not isinstance(values, dict):
        return []
    return [r for r in values.values() if isinstance(r, dict)
            and r.get('status') == 'passed'
            and r.get('bounded_rays') is True
            and r.get('cpu_comparison') is True
            and isinstance(r.get('instance_transform'), int)
            and not isinstance(r.get('instance_transform'), bool)
            and r.get('instance_transform') in range(6)
            and r.get('validation_enabled') is True
            and r.get('validation_errors') == 0
            and r.get('validation_warnings') == 0]


def _state():
    boundary = _receipt('ray-boundary-results.json')
    progressive = _receipt('instance-progressive-results.json')
    candidate = boundary.get('results', {}).get('candidate', {}) if boundary else {}
    numeric = boundary.get('independent_numeric', {}).get('candidate', {}) if boundary else {}
    passed = bool(boundary and boundary.get('status') == 'passed'
                  and boundary.get('harness_checks_passed') is True
                  and boundary.get('input_hashes_unchanged') is True
                  and candidate.get('status') == 'passed'
                  and candidate.get('cases') == 360 and candidate.get('failed_cases') == 0
                  and candidate.get('bad_transform') is False
                  and candidate.get('bad_spawn') is False
                  and candidate.get('omit_dispatch') is False
                  and numeric.get('failed_cases') == 0
                  and numeric.get('numeric_readback_rechecked') is True
                  and numeric.get('observed_exact_vertices', 0) >= 12
                  and numeric.get('observed_exact_edges', 0) >= 12)
    scenes = _scene_results(progressive)
    scene_pass = bool(progressive and progressive.get('status') == 'passed'
                      and {r['instance_transform'] for r in scenes} == set(range(6)))
    return boundary, progressive, candidate, numeric, passed, scenes, scene_pass


def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Nonfinite or missing selected instance result')
    return format(value, '.8g')


def apply(sections, sources, source, section, table, cite):
    boundary, progressive, candidate, numeric, passed, scenes, scene_pass = _state()
    source('S100', 'Basix hardware boundary and instance fixture acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/ray-boundary-results.json',
           ('The selected canonical harness passes 360 hardware boundary/instance fixtures and verifies wrong-transform, invalid-spawn, omitted-dispatch and actual no-op-shader controls. A separate Decimal oracle rechecks fixed input catalogs, numeric readback, plane membership and actual edge/vertex observations.'
            if passed else 'Hardware boundary and fixed instance-catalog fixtures are under evaluation. This documentation build has no passing selected canonical receipt; preliminary scratch runs do not certify a completed gate.'),
           kind='local measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
           rights='Original Basix code, fixtures and selected numerical receipts MIT.')
    source('S101', 'Vulkan: VkAccelerationStructureInstanceKHR',
           'https://docs.vulkan.org/refpages/latest/refpages/source/VkAccelerationStructureInstanceKHR.html',
           'A top-level acceleration-structure instance references bottom-level geometry at a transformed location. Its transform and application-defined instance index have distinct roles. Basix tests one actual transformed TLAS instance against separately baked world-space metadata; it does not thereby implement a general per-instance geometry mapping.',
           rights='Official specification linked and paraphrased; no implementation copied.')
    source('S102', 'Vulkan: VkTransformMatrixKHR',
           'https://docs.vulkan.org/refpages/latest/refpages/source/VkTransformMatrixKHR.html',
           'The instance transform is a row-major 3-by-4 affine matrix whose first three columns must define an invertible 3-by-3 matrix. The Basix fixture catalog uses six known invertible families; accepting this bounded catalog does not validate arbitrary user matrices or provide a universal transformation-error bound.',
           rights='Official specification linked and paraphrased; no implementation copied.')
    source('S103', 'Basix fixed instance-catalog progressive acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/instance-progressive-results.json',
           ('The selected actual-renderer receipt covers all six instance-transform IDs with bounded rays and CPU world-space image comparisons. The object-space BLAS and TLAS transform are separate from world-space shader metadata, light areas and camera setup. Broader instance management and transform error propagation remain open.'
            if scene_pass else 'The actual renderer has a bounded instance-transform selector, but this documentation build awaits selected CPU/GPU scene acceptance across all six catalog IDs. Hardware fixture acceptance alone does not certify rendered transport.'),
           kind='local measured scoped correctness' if scene_pass else 'implementation awaiting canonical acceptance',
           rights='Original Basix implementation and selected numerical receipts MIT.')
    by = {s['id']: s for s in sections}
    body = by['reference-renderer']['body']
    if passed:
        body = body.replace(
            'Hardware boundary hits, transformed instances, imported/textured scenes, motion/depth guide conventions and dynamic DLSS recovery remain open.',
            'Hardware boundary hits and a fixed transformed-instance catalog now have separate scoped evidence below. General instance metadata/error propagation, imported/textured scenes, motion/depth guide conventions and dynamic DLSS recovery remain open.')
        body = body.replace(
            'Exact hardware edge/vertex barycentrics, arbitrary transforms, subnormal arithmetic and other GPUs remain unverified.',
            'This earlier identity-instance gate did not exercise exact hardware boundary barycentrics or transformed instances. The later catalog gate below is separate; arbitrary transforms, subnormal arithmetic and other GPUs remain unverified.')
        body = body.replace(
            '<strong>Next acceptance:</strong> exercise hardware edge/vertex hit barycentrics, varied triangle orientations and transformed instances before broader adoption. Then extend imported geometry, depth/motion conventions and temporal DLSS reset/recovery.',
            '<strong>Later acceptance:</strong> the separate hardware boundary and fixed transformed-instance gate below extends this earlier scope. General instance metadata/error propagation, imported geometry, depth/motion conventions and temporal DLSS reset/recovery remain subsequent gates.')
    body += '''<h3 id="ray-boundaries">Hardware boundaries and a fixed instance catalog</h3>
<div class="callout"><strong>Keep the candidate opt-in</strong><p><code>--bounded-rays</code> retains its empirical traversal allowance. Actual boundary hits and a small transform catalog can establish named correctness results; they do not supply a universal Vulkan error bound. The legacy offset policy remains the default, and M2 remains partial.</p></div>
<p>The fixture builds an object-space BLAS, places it through an actual TLAS instance transform, and separately bakes FP32 world-space triangle metadata. The shader reads hardware barycentrics and uses the world triangle for reconstruction and spawning. This is one static instance with a known primitive mapping. A general engine still needs instance-to-geometry lookup, transform updates, lifetime/residency handling and a justified transform-error policy.</p>''' + cite('S101', 'S102') + '''
<figure><div style="overflow-x:auto"><svg viewBox="0 0 780 176" role="img" aria-labelledby="boundary-flow-title boundary-flow-desc" style="width:100%;min-width:660px;height:auto;max-width:780px"><title id="boundary-flow-title">Two geometry paths and an independent boundary oracle</title><desc id="boundary-flow-desc">The object BLAS and TLAS matrix drive hardware intersections. Separately baked world triangles supply shader metadata. A Decimal oracle checks both input catalogs and the serialized GPU results.</desc><defs><marker id="boundary-flow-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="currentColor"/></marker></defs><g fill="none" stroke="currentColor" stroke-width="1.3"><rect x="6" y="16" width="190" height="56" rx="8"/><rect x="6" y="106" width="190" height="56" rx="8"/><rect x="281" y="16" width="205" height="56" rx="8"/><rect x="281" y="106" width="205" height="56" rx="8"/><rect x="571" y="60" width="203" height="62" rx="8"/><path d="M196 44 H274" marker-end="url(#boundary-flow-arrow)"/><path d="M196 134 H274" marker-end="url(#boundary-flow-arrow)"/><path d="M486 44 H526 V80 H564" marker-end="url(#boundary-flow-arrow)"/><path d="M486 134 H526 V102 H564" marker-end="url(#boundary-flow-arrow)"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="14" text-anchor="middle"><text x="101" y="40">Object BLAS</text><text x="101" y="59">and TLAS matrix</text><text x="101" y="130">Separately baked</text><text x="101" y="149">world triangle metadata</text><text x="383" y="40">Hardware query</text><text x="383" y="59">and barycentrics</text><text x="383" y="130">Shader reconstruction</text><text x="383" y="149">and ray spawn</text><text x="672" y="86">Independent Decimal</text><text x="672" y="106">catalog / geometry checks</text></g></svg></div><figcaption>The checker validates both the requested inputs and returned geometry. Matching two shader-side formulas would be a weaker test.</figcaption></figure>
<h4>What the boundary oracle checks</h4>
<p>A four-triangle fan gives a shared center vertex and shared edges. Targets include the center, an edge point, an interior point, and points on opposite sides of an edge separated by a small fixed input displacement. The oracle accepts a primitive tie only when the requested point actually belongs to the returned uploaded triangle within the declared membership tolerance. Nearby targets therefore cannot pass through arbitrary primitive-ID equivalence.</p>
<p>The independent Python checker uses Decimal arithmetic at precision 60. It validates the frozen transform, object-geometry and target catalogs; checks hardware barycentric domain, geometric normal, ray/point/barycentric agreement, plane membership and secondary intersections; and verifies the spawned point stays on the outgoing side before the actual blocker plane. It recomputes outcomes from numeric readback rather than trusting host pass flags. Position and membership tolerances remain explicit fixture acceptance limits, not hardware theorems.</p>'''
    body += table(['Catalog ID', 'Fixed transform / role'], [
        ('0', 'Identity control.'),
        ('1', '90° rotation about Y; exercises a different world-space plane orientation.'),
        ('2', 'Nonuniform scale diag(2, 0.5, 4).'),
        ('3', 'Shear: x receives 0.25y and z receives 0.25x, both from the original input coordinates.'),
        ('4', 'Reflection across X; boundary fixtures retain transformed winding, while rendered scenes compensate it to preserve authored sides.'),
        ('5', 'Translation (0.125, −0.25, 0.5), scaled by fixture size; actual renderer catalog uses unit scale.'),
    ])
    body += '<p>The fixture cross-product has six transforms, three scales (0.001, 1, 1000), two sides, five target families and two layer states: 360 cases. The layer states distinguish a correct self-miss from a nearby secondary blocker. Acceptance requires at least 12 observed exact hardware vertex hits and 12 exact edge hits; a nominal target label alone cannot satisfy that requirement.</p>'
    if passed:
        body += ('<p><strong>Selected canonical boundary gate passes:</strong> all 360 candidate cases pass. The independent checker records '
                 + _number(numeric['observed_exact_vertices']) + ' exact vertex hits and '
                 + _number(numeric['observed_exact_edges']) + ' exact edge hits. The five-run harness also verifies rejection controls for a wrong TLAS transform, invalid spawn, omitted dispatch and an actually compiled no-op shader. Frozen input hashes and whole-process core/SyncVal logs are checked.</p>') + cite('S100')
        controls = [r for name, r in boundary.get('results', {}).items() if name != 'candidate']
        if any('fixtures' not in r for r in controls):
            body += '<p>The public projection keeps complete candidate readback and compact control summaries. The frozen full negative-control receipts remain local; compact summaries do not expose every readback.</p>'
    else:
        body += '<p><strong>Canonical boundary acceptance pending:</strong> no passing selected boundary receipt is available to this build. Development runs are retained separately.</p>' + cite('S100')
    body += '<p><strong>Failure-control lesson:</strong> a wrong TLAS transform can still return the expected primitive index. The independent oracle also checks that the returned secondary distance reaches that uploaded world-space blocker plane; identity alone is insufficient.</p>' + cite('S100')
    body += '''<h4>Actual progressive renderer integration</h4>
<p><code>--instance-transform 0..5</code> selects the same bounded catalog in the actual renderer. The BLAS remains in object space; the TLAS carries the instance matrix. The CPU comparison uses separately transformed world geometry, and shader metadata, emitter areas and camera setup follow that world scene. This is a static single-instance route, rather than an importer or general instance manager.</p>'''
    if scene_pass:
        body += '<p><strong>Reflections preserve authored sides:</strong> the renderer reverses object-triangle winding before both BLAS construction and world baking for the reflected catalog entry. An independent normal-orientation check guards outward and one-sided emitter behavior. Omitting that compensation is rejected during CPU preprocessing; it is a different control from a GPU shader fault. Additional nonuniform-scale and shear cases combine the instance transform with large rebased world translations. The reflected catalog rebuilds its object BLAS with the compensated winding. A future positive/reflected pair sharing one BLAS needs per-instance orientation handling; globally reversing that shared mesh would change both objects.</p>' + cite('S103')
        body += '<p><strong>Selected rendered integration passes:</strong> all six catalog IDs have bounded-ray CPU/GPU comparisons.'
        metric_keys = ('normalized_rmse', 'low_radiance_normalized_rmse', 'max_channel_mean_relative_error')
        if all(all(isinstance(r.get(k), (int, float)) and not isinstance(r.get(k), bool) and math.isfinite(r[k]) for k in metric_keys) for r in scenes):
            body += (' Worst measured global normalized RMSE is ' + _number(max(r[metric_keys[0]] for r in scenes))
                     + ', low-radiance normalized RMSE ' + _number(max(r[metric_keys[1]] for r in scenes))
                     + ', and channel-mean relative error ' + _number(max(r[metric_keys[2]] for r in scenes)) + '.')
        body += ' These are fixed-fixture correctness results, with the selected receipt defining settings and limits; no comparative performance or temporal-quality claim follows.</p>' + cite('S103')
    else:
        body += '<p><strong>Rendered catalog acceptance pending:</strong> a selected receipt must cover all six IDs with CPU comparison enabled. Passing boundary fixtures alone is insufficient.</p>' + cite('S103')
    body += '''<p><strong>Next scope:</strong> keep bounded rays opt-in while implementing general instance metadata and transform-error propagation. Multi-instance remapping, arbitrary user transforms, updates/motion, subnormal behavior, other GPUs and temporal rendering remain open. Imported geometry and depth/motion-guide conventions still need separate acceptance. The retained legacy NDF sampling discrepancy is unchanged.</p>'''
    links = ['<a href="#roadmap">Milestones and transition log</a>']
    if boundary:links.append('<a href="ray-boundary-results.json">Boundary and transform fixture receipt</a>')
    if progressive:links.append('<a href="instance-progressive-results.json">Instance-catalog renderer receipt</a>')
    body += '<p>' + ' · '.join(links) + '</p>'
    by['reference-renderer']['body'] = body
    status = 'passes its selected 360-case boundary gate' if passed else 'awaits selected canonical boundary acceptance'
    by['summary']['body'] += '<p><strong>Hardware boundary update:</strong> the opt-in candidate ' + status + '. A separately baked world counterpart checks actual TLAS transforms. <a href="#ray-boundaries">Catalog, measured edge/vertex observations and remaining scope</a>.</p>' + cite('S100')
    by['lessons']['body'] += '<p><strong>Validate the input catalog as well as the output:</strong> six transform labels are weak evidence if every matrix accidentally remains identity. Independent catalog checks, actual edge/vertex observations and a wrong-transform rejection control make the acceptance meaningful. Keep shared-boundary primitive ties tied to geometric membership, and distinguish a fixed catalog result from a general instance/error model.</p>' + cite('S100', 'S101')


def enrich_library(library):
    boundary, progressive, candidate, numeric, passed, scenes, scene_pass = _state()
    library['summary'].append('Hardware edge/vertex and fixed TLAS-instance catalog '
                             + ('passes its selected boundary gate' if passed else 'awaits selected canonical acceptance')
                             + '; bounded rays remain opt-in and broader M2 stays partial.')
    library['findings'].append(dict(
        id='F22', title='Exact boundary observations and independent instance catalogs strengthen a bounded ray-spawn gate',
        question_ids=['Q2'], topic='rendering',
        claim='An object-space BLAS with an actual TLAS matrix is checked against separately baked world triangles. Decimal input-catalog and geometric readback checks distinguish meaningful hardware edge/vertex and transform coverage from labels or matching formulas.',
        evidence_type='measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
        confidence='high' if passed else 'provisional',
        context='2026-10-28; original MIT fixtures/checker; six fixed transform families on one Windows RTX4090/NVIDIA617.42 configuration.',
        strengths='Independent matrix/object/target catalogs, Decimal geometry, exact hardware boundary counts, shared-edge membership, numeric spawn/blocker checks and actual fault controls.',
        limitations='Single static TLAS instance and a fixed transform catalog. The traversal allowance remains empirical; general instance remapping, arbitrary transform uncertainty, motion, subnormal behavior, cross-device quality and performance remain open.',
        implications='Retain --bounded-rays as opt-in; implement general instance metadata/error propagation before broader adoption, then validate imported scenes and temporal guides separately.',
        tags=['Vulkan', 'TLAS', 'instance transform', 'barycentrics', 'ray origin', 'independent oracle'],
        evidence=[dict(source_id=s, locator='Selected boundary/renderer receipt and native transform contract', relation='supports',
                       note='API semantics, bounded numerical fixtures and rendered integration are distinct evidence types.')
                  for s in ['S100', 'S101', 'S102', 'S103']]))
    for finding in library['findings']:
        if finding['id'] == 'F21':
            finding['context'] += ' Earlier identity-instance gate; later hardware boundary/catalog evidence is F22.'
            finding['implications'] = 'Retain the original identity-instance receipt. Use F22 for later scoped boundary/catalog evidence; general instance metadata, transform uncertainty and temporal integration remain open.'
    library['actions'][0].update(
        title='Complete general instance metadata, scene and temporal acceptance', finding_ids=['F19', 'F20', 'F21', 'F22'],
        rationale='The fixed boundary and instance catalog extends earlier numerical gates without establishing an arbitrary-transform or multi-instance renderer.',
        success_criterion='Selected boundary and all-six-ID rendered receipts pass; general instance-to-geometry metadata, transform error propagation, imported/textured scenes and depth/motion/reset behavior then pass separately.')
    library['scope_changes'].append('Added hardware edge/vertex observations, a fixed actual TLAS transform catalog and an independent Decimal input/geometry oracle. Catalog acceptance remains distinct from general instance management, arbitrary transform bounds and temporal rendering.')
