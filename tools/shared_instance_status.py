"""Receipt-driven bounded shared-BLAS identity, authored-side and update evidence."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import json
import math

DOC = Path(__file__).resolve().parents[1] / 'docs'


def _state():
    path = DOC / 'shared-instance-results.json'
    if not path.exists():
        return None, {}, {}, False
    receipt = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(receipt, dict):
        raise ValueError('Expected selected shared-instance receipt object')
    candidate = receipt.get('results', {}).get('candidate', {})
    numeric = receipt.get('independent_numeric', {}).get('candidate', {})
    larger = receipt.get('results', {}).get('larger-preview', {})
    larger_numeric = receipt.get('independent_numeric', {}).get('larger-preview', {})
    passed = bool(receipt.get('status') == 'passed'
                  and receipt.get('harness_checks_passed') is True
                  and receipt.get('input_hashes_unchanged') is True
                  and candidate.get('status') == 'passed'
                  and candidate.get('fault') == ''
                  and candidate.get('instances_per_scene') == 2
                  and candidate.get('blas_per_scene') == 1
                  and candidate.get('probe_cases') == 24
                  and candidate.get('failed_probe_cases') == 0
                  and candidate.get('probe_tlas_updates') == 2
                  and candidate.get('render_phases') == 3
                  and candidate.get('render_tlas_updates') == 2
                  and candidate.get('inflight_rejections') == 1
                  and candidate.get('inflight_record_rejections') == 1
                  and candidate.get('inflight_poison_rejections') == 1
                  and all(candidate.get(k) is True for k in ('probe_blas_identity', 'probe_tlas_identity',
                                                           'render_blas_identity', 'render_tlas_identity',
                                                           'bounded_rays', 'validation_enabled'))
                  and candidate.get('validation_errors') == 0
                  and candidate.get('validation_warnings') == 0
                  and candidate.get('root_bytes') == 192
                  and candidate.get('instance_bytes') == 16
                  and numeric.get('cases') == 24
                  and numeric.get('failed_cases') == 0
                  and numeric.get('numeric_readback_rechecked') is True
                  and numeric.get('independent_frozen_target_normal_catalog') is True
                  and numeric.get('large_stable_id_verified') is True
                  and len(receipt.get('cases', [])) == 10
                  and larger.get('status') == 'passed'
                  and larger.get('width') == 128 and larger.get('height') == 64
                  and larger.get('spp') == 16
                  and larger_numeric.get('failed_cases') == 0
                  and len(numeric.get('independent_render_clearance', [])) == 3
                  and len(larger_numeric.get('independent_render_clearance', [])) == 3
                  and all(row.get('independent_geometry_clearance_checked') is True
                          for result in (numeric, larger_numeric)
                          for row in result.get('independent_render_clearance', [])))
    return receipt, candidate, numeric, passed


def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Missing/nonfinite selected shared-instance metric')
    return format(value, '.8g')


def apply(sections, sources, source, section, table, cite):
    receipt, candidate, numeric, passed = _state()
    source('S104', 'Basix shared-BLAS identity and synchronized update acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/shared-instance-results.json',
           ('The selected canonical receipt passes the bounded two-object shared-BLAS experiment. Independent probe catalogs/readback checks, rendered CPU comparisons, metadata epochs and held-submission refusals have separate acceptance. Wrong-slot, epoch, range, orientation, omitted-dispatch, skipped-update, reported-distance and compiled no-op controls expose named failures.'
            if passed else 'Two-object shared-BLAS identity and update work is under evaluation. No passing selected canonical receipt is available to this build; preliminary scratch results do not certify a completed scope.'),
           kind='local measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
           rights='Original Basix code, fixtures and selected numerical receipts MIT; private raw logs and telemetry excluded.')
    source('S105', 'Slang RayQuery custom-instance and primitive queries',
           'https://shader-slang.org/stdlib-reference/types/rayquery-03/index.html',
           'The RayQuery reference exposes custom-instance-index and primitive-index queries separately from instance-order queries. Basix calls CommittedRayInstanceCustomIndex to select a dense metadata record, then preserves the application stable ID as a separate value. Method availability and the actual pinned compiler remain part of implementation acceptance.',
           rights='Official standard-library reference linked and paraphrased; no implementation copied.')
    source('S106', 'Vulkan acceleration-structure build and update contract',
           'https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBuildAccelerationStructuresKHR.html',
           'Update requires a source built with ALLOW_UPDATE and compatible geometry/count/type/flags. Scratch, inputs and acceleration-structure accesses require synchronization; input/scratch memory can be retired after completion. Basix exercises a fixed-count in-place TLAS update on one owned queue and retains prior buffers until that submission completes. This does not establish asynchronous or multiqueue recovery.',
           rights='Official specification linked and paraphrased; no implementation copied.')
    by = {s['id']: s for s in sections}
    body = by['reference-renderer']['body']
    if passed:
        body = body.replace(
            'A general engine still needs instance-to-geometry lookup, transform updates, lifetime/residency handling and a justified transform-error policy.',
            'This earlier catalog gate did not establish shared-instance lookup or updates. The later shared-BLAS experiment below adds bounded metadata/update evidence; general lifetime/residency and transform-error policies remain open.')
        body = body.replace(
            'A future positive/reflected pair sharing one BLAS needs per-instance orientation handling; globally reversing that shared mesh would change both objects.',
            'The shared-BLAS path below uses per-instance authored orientation while preserving vertex order. Globally reversing a shared mesh would still change both objects, so the earlier catalog rebuild remains a separate smoke-test route.')
        body = body.replace(
            '<strong>Next scope:</strong> keep bounded rays opt-in while implementing general instance metadata and transform-error propagation. Multi-instance remapping, arbitrary user transforms, updates/motion, subnormal behavior, other GPUs and temporal rendering remain open.',
            '<strong>Later scope:</strong> the shared-BLAS gate below adds bounded instance remapping and synchronized transform updates. General transform uncertainty, asynchronous/multiqueue updates, subnormal behavior, other GPUs and temporal rendering remain open. Bounded rays stay opt-in.')
    body += '''<h3 id="shared-instances">Shared BLAS · identity, authored sides and synchronized updates</h3>
<div class="callout"><strong>One object BLAS, two differently transformed instances</strong><p>The new route keeps object-space triangles unchanged. A hardware custom-instance index selects a dense metadata record containing a triangle range, stable instance ID and scene epoch. The shader validates that range and epoch before combining the local hardware primitive index with the range offset. Stable application IDs remain separate from array order.</p></div>''' + cite('S104', 'S105') + '''
<figure><div style="overflow-x:auto"><svg viewBox="0 0 820 172" role="img" aria-labelledby="shared-flow-title shared-flow-desc" style="width:100%;min-width:700px;height:auto;max-width:820px"><title id="shared-flow-title">Shared geometry with per-instance identity and orientation</title><desc id="shared-flow-desc">Two TLAS entries share one unchanged object BLAS. The hardware custom index selects an epoch-checked metadata record. Its triangle range maps the local primitive to world metadata while preserving separate stable IDs and authored orientation.</desc><defs><marker id="shared-flow-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="currentColor"/></marker></defs><g fill="none" stroke="currentColor" stroke-width="1.3"><rect x="6" y="54" width="162" height="66" rx="8"/><rect x="225" y="16" width="172" height="60" rx="8"/><rect x="225" y="106" width="172" height="60" rx="8"/><rect x="455" y="54" width="172" height="66" rx="8"/><rect x="684" y="54" width="130" height="66" rx="8"/><path d="M168 75 H196 V46 H218" marker-end="url(#shared-flow-arrow)"/><path d="M168 99 H196 V136 H218" marker-end="url(#shared-flow-arrow)"/><path d="M397 46 H426 V75 H448" marker-end="url(#shared-flow-arrow)"/><path d="M397 136 H426 V99 H448" marker-end="url(#shared-flow-arrow)"/><path d="M627 87 H677" marker-end="url(#shared-flow-arrow)"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="13.5" text-anchor="middle"><text x="87" y="81">Unchanged object BLAS</text><text x="87" y="102">shared triangle order</text><text x="311" y="41">TLAS instance A</text><text x="311" y="62">positive transform</text><text x="311" y="131">TLAS instance B</text><text x="311" y="152">reflected transform</text><text x="541" y="76">Custom slot → metadata</text><text x="541" y="97">range · stable ID · epoch</text><text x="749" y="76">World triangle</text><text x="749" y="97">authored side</text></g></svg></div><figcaption>Dense slots may change between epochs while stable instance and primitive IDs stay attached to the same authored objects.</figcaption></figure>
<h4>Keep orientation independent of vertex order</h4>
<p>A reflected transform reverses geometric winding. Reversing the shared BLAS would affect every instance, so the new path retains object and shader vertex order and carries a per-instance orientation sign. The CPU reference has an explicit <code>reverse_orientation</code> flag for authored geometric/emitting sides; its default remains false. Its intersection and emitter normals follow that flag without changing triangle sample order.</p>
<p>The first shared rendered experiment passed its declared image limits but showed a larger paired discrepancy because a reference-only vertex swap also changed sampled emitter locations for the same random values. Explicit authored orientation preserves both physical sides and sample correspondence. The BRDF, declared image limits and legacy default are unchanged. Separate offline suites cover 68 instance checks and 85 CPU reference checks, including the new authored-side cases.</p>''' + cite('S104')
    body += table(['Data / check', 'Bounded contract'], [
        ('GPU root / instance record', 'Root 192 bytes; instance record 16 bytes: first triangle, count, stable ID and epoch. Earlier 176-byte root receipts describe the prior ABI.'),
        ('Identity and material probes', '24 targeted rays: three epochs × two objects × two local primitives × two sides. Stable IDs 10000123 and 90000007 are checked as integers; the latter exceeds exact float32 integer range.'),
        ('Stable primitive identity', 'Each instance has a separate primitive-ID base. Hardware local primitive, dense world-triangle index, stable global primitive ID and material-table index are checked separately.'),
        ('Epoch sequence', 'Initial A/B slots; moved geometry with the same slots; restored geometry with B/A slots. Motion-only update and slot remapping therefore have separate evidence.'),
        ('Input domain', 'Finite invertible matrices with condition∞ at most 10⁶, valid IDs/material overrides and representable nondegenerate FP32 triangles. This CPU input policy is not a hardware uncertainty bound.'),
    ])
    body += '''<h4>Update only after readers retire</h4>
<p>Each probe/render scene uses one shared BLAS and a fixed two-instance TLAS. The TLAS is built with <code>ALLOW_UPDATE</code> and updated in place twice with matching build configuration and update scratch allocation. Instance buffers survive until their build/update submission completes. A held submission makes the timeline observably incomplete: TLAS update, command recording and output poisoning must refuse before mutating referenced roots or metadata. Releasing the gate and waiting for completion permits the next update.</p>
<p>The rendered stage uses two original box scenes over three metadata epochs, compares each with the independently constructed CPU world scene, and checks primary intersections plus batch/reset behavior. Updates and retirement are synchronous on one owned queue. World triangle metadata is still baked per instance, so sharing the hardware BLAS does not by itself solve scalable metadata storage or general asset residency.</p>''' + cite('S104', 'S106')
    body += '<p>A larger preview exposed a fixture ambiguity: moved floor triangles from different instances overlapped on the same plane. Reusing the exact submitted GPU ray did not resolve the strict primitive-identity disagreement. The fix separates only rendered room placement by ±0.7 in X after phase mutations, retaining the probe catalog and strict oracle. Runtime float geometry bounds must be disjoint; the harness independently recomputes those bounds from the serialized actual matrices. This fixes the fixture rather than broadening tie acceptance.</p>' + cite('S104')
    if passed:
        body += ('<div class="callout"><strong>Selected shared-instance gate passes</strong><p>All 24 identity/material/orientation probes pass. The selected run verifies two in-place TLAS updates in each scene, stable BLAS/TLAS identities, all three in-flight refusal checks and three rendered epochs. Worst measured global normalized RMSE is '
                 + _number(candidate['worst_normalized_rmse']) + ', low-radiance normalized RMSE '
                 + _number(candidate['worst_low_radiance_rmse']) + ', and channel-mean relative error '
                 + _number(candidate['worst_mean_relative']) + '. The respective limits remain 0.05, 0.10 and 0.02.</p></div>') + cite('S104')
        larger = receipt['results']['larger-preview']
        body += '<p>The selected ten-run harness includes a second positive experiment at 128×64/16SPP, with three rendered epochs and independently checked room clearance. Its worst global normalized RMSE is ' + _number(larger['worst_normalized_rmse']) + '. Each positive experiment retains its own 24 probes and three image comparisons.</p>' + cite('S104')
        if (DOC / 'assets/shared-blas-cornell.png').is_file():
            body += '<figure><img src="assets/shared-blas-cornell.png" alt="Two separately placed Cornell-style rooms rendered from one shared object BLAS" loading="lazy" style="width:100%;max-width:960px;height:auto"><figcaption>Measured local Vulkan path-traced preview: two original box rooms, one shared BLAS, 128×64 at128SPP. This separate visual run passed three epochs with worst global normalized RMSE4.4793936462×10⁻⁶ and byte-identical restored HDR. This is a bounded correctness preview, with no UE-quality or performance comparison.</figcaption></figure>' + cite('S104')
        decoded = receipt.get('hdr', {}).get('independently_decoded_restore_hdr_identity')
        if decoded is True:
            body += '<p>The independently decoded first and restored final HDR files are byte-identical. This is a named restoration result for the tested scene and device, not a guarantee for all acceleration-structure rebuilds.</p>' + cite('S104')
        body += '<p>The canonical harness also tests wrong custom slots, stale epochs, invalid ranges, incorrect authored orientation, omitted dispatch, skipped TLAS updates and a 10% bias in the shader-reported hit distance. Its independently reconstructed ray/point equation prevents matching IDs from hiding a stale or biased distance. An actually dispatched compiled no-op shader must leave poisoned output and be rejected. Input hashes and complete core/SyncVal process logs are checked through teardown.</p>' + cite('S104')
        controls = [r for name, r in receipt.get('results', {}).items() if name != 'candidate']
        if any('probes' not in r for r in controls):
            body += '<p>The selected public projection retains complete positive probes/images and compact negative summaries. Frozen full negative readbacks remain local with the raw receipt hash.</p>'
    else:
        body += '<p><strong>Canonical acceptance pending:</strong> no passing selected shared-instance receipt is available to this build. Implementation and preliminary scratch runs do not complete the static-shared or update gates.</p>' + cite('S104')
    body += '''<p><strong>Remaining scope:</strong> M2-gpu-instances and the broader renderer remain partial. Keep <code>--bounded-rays</code> opt-in while validating broader transform-conditioned uncertainty and ranges. Asynchronous/multiqueue updates, device-loss recovery, general asset/residency handling, subnormal behavior, other GPUs and performance remain unaccepted. Scene importing, textures and depth/motion guides for temporal DLSS are subsequent gates. The retained 18-case legacy NDF discrepancy remains separate.</p>'''
    if receipt:
        body += '<p><a href="shared-instance-results.json">Shared-instance acceptance receipt</a> · <a href="#roadmap">Milestones and transition history</a></p>'
    by['reference-renderer']['body'] = body
    status = 'passes its selected bounded identity/update gate' if passed else 'is undergoing separate canonical identity/update acceptance'
    by['summary']['body'] += '<p><strong>Shared-BLAS update:</strong> the two-object experiment ' + status + '. Stable IDs, authored orientation and metadata epochs are distinct contracts. <a href="#shared-instances">Mapping, synchronization, measured results and remaining limits</a>.</p>' + cite('S104')
    by['lessons']['body'] += '<p><strong>Separate geometry, identity and lifetime:</strong> matching primitive labels cannot certify a moved scene if the reported hit distance disagrees with the ray and reconstructed point. Likewise, an update refusal is only safe when it occurs before referenced root or metadata writes. Explicit authored orientation preserves emitter sides and random-sample correspondence without changing shared vertex order. Larger previews can expose coplanar cross-instance fixture overlap missed by a small image: verify actual transformed bounds and retain strict hit checks.</p>' + cite('S104')


def enrich_library(library):
    receipt, candidate, numeric, passed = _state()
    library['summary'].append('Two-object shared-BLAS identity and synchronized updates '
                             + ('pass the selected bounded gate' if passed else 'await selected canonical acceptance')
                             + '; empirical uncertainty and broader renderer integration remain open.')
    library['findings'].append(dict(
        id='F23', title='Per-instance identity, authored sides and refusal order need separate acceptance',
        question_ids=['Q2'], topic='rendering',
        claim='Dense custom slots select epoch-checked triangle spans while stable IDs remain independent of slot order. Per-instance authored orientation preserves shared vertex order. Numeric ray/point checks and held-submission refusals test identity and lifetime separately.',
        evidence_type='measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
        confidence='high' if passed else 'provisional',
        context='2026-10-28; original MIT shared-BLAS experiment; two objects, three epochs and one owned queue on the named RTX4090/NVIDIA617.42 configuration.',
        strengths='Independent fixed probe catalogs, integer IDs above float32 exact range, stable primitive/material checks, ray-distance equations, actual in-place TLAS updates and before-write refusal controls.',
        limitations='Bounded two-instance scenes with world metadata baked per instance. Matrix input checks do not prove hardware uncertainty; async/multiqueue/device-loss recovery, broad scene integration, other devices and performance remain open.',
        implications='Keep bounded rays opt-in and general M2 partial; broaden transform uncertainty/ranges before importer and temporal-guide integration. Preserve earlier failed controls and physical/sampling conventions.',
        tags=['Vulkan', 'shared BLAS', 'instance metadata', 'authored orientation', 'TLAS update', 'lifetime', 'independent oracle'],
        evidence=[dict(source_id=s, locator='Selected identity/update receipt or native query/update contract', relation='supports',
                       note='API requirements, bounded execution and broader adoption are separate claims.')
                  for s in ['S104', 'S105', 'S106']]))
    for finding in library['findings']:
        if finding['id'] == 'F22':
            finding['context'] += ' Earlier single-instance catalog; later bounded shared-BLAS identity/update evidence is F23.'
            finding['implications'] = 'Retain the original catalog/boundary receipt. F23 adds bounded shared-instance lookup and synchronized update evidence; general transform uncertainty, async lifetime and temporal integration remain open.'
    library['actions'][0].update(
        title='Extend shared-instance uncertainty and scene integration', finding_ids=['F19', 'F20', 'F21', 'F22', 'F23'],
        rationale='Bounded shared-BLAS identity and synchronized updates extend the static catalog without proving a general uncertainty or asynchronous resource model.',
        success_criterion='Selected shared-instance receipt passes independent IDs/materials, ray-distance and refusal controls; broader transform/range, importer/textures and depth/motion/reset gates then pass separately.')
    library['scope_changes'].append('Added bounded two-object shared-BLAS metadata, authored orientation independent of vertex order, three-epoch identity/remapping checks and synchronous in-place TLAS update acceptance. Root ABI extends from176to192bytes; earlier receipts stay scoped to their original versions. No general hardware-error, asynchronous update or performance claim is promoted.')
