"""Receipt-driven transform arithmetic and conditional traversal policy docs."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import json
import math

DOC = Path(__file__).resolve().parents[1] / 'docs'


def _state():
    path = DOC / 'transform-ray-results.json'
    if not path.is_file():
        return None, False
    receipt = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(receipt, dict):
        raise ValueError('Expected selected transform-ray receipt object')
    results = receipt.get('results', {})
    numeric = receipt.get('independent_numeric', {})
    names = ('candidate', 'fault-zero-budget', 'fault-negative-budget',
             'fault-origin', 'fault-direction', 'fault-omit', 'compiled-no-op')
    positive, proof = results.get('candidate', {}), numeric.get('candidate', {})
    reflected = receipt.get('abi_reflection', {})
    types = reflected.get('reflected_types', {})
    renderer = receipt.get('renderer_acceptance', {})
    passed = (receipt.get('status') == 'passed'
              and receipt.get('harness_checks_passed') is True
              and receipt.get('input_hashes_unchanged') is True
              and receipt.get('loader_confirmed_validation') is True
              and receipt.get('synchronization_validation_requested') is True
              and len(receipt.get('cases', [])) == 7
              and all(name in results and name in numeric for name in names)
              and positive.get('status') == 'passed'
              and positive.get('cases') == 96 and positive.get('failed_cases') == 0
              and positive.get('root_bytes') == 208 and positive.get('triangle_bytes') == 80
              and positive.get('exact_vertices') == 24 and positive.get('exact_edges') == 24
              and proof.get('failed_cases') == 0
              and proof.get('observed_exact_vertices') == 24
              and proof.get('observed_exact_edges') == 24
              and all(numeric[name].get('cases') == 96
                      and numeric[name].get('numeric_readback_rechecked') is True
                      and numeric[name].get('host_disagreements') == [] for name in names)
              and all(results[name].get('failed_cases') == 96
                      and numeric[name].get('failed_cases') == 96 for name in names[1:])
              and all(results[name].get('validation_errors') == 0
                      and results[name].get('validation_warnings') == 0 for name in names)
              and reflected.get('status') == 'passed'
              and reflected.get('compiled_shader_byte_identity') is True
              and types.get('Root', {}).get('bytes') == 208
              and types.get('Root', {}).get('offsets', {}).get('rayDomain') == 192
              and types.get('Triangle', {}).get('bytes') == 80
              and types.get('Triangle', {}).get('offsets', {}).get('transformError') == 64
              and types.get('Instance', {}).get('bytes') == 16
              and receipt.get('cpu_checks') == 78
              and renderer.get('control_hdr_identity') is True
              and all(renderer.get(name, {}).get('status') == 'passed'
                      and renderer.get(name, {}).get('harness_checks_passed') is True
                      and renderer.get(name, {}).get('harness_checks') == 10
                      and renderer.get(name, {}).get('input_hashes_unchanged') is True
                      and renderer.get(name, {}).get('positive_probe_cases') == 48
                      and renderer.get(name, {}).get('rendered_phases') == 6
                      for name in ('candidate', 'control')))
    return receipt, passed


def _metric(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Missing/nonfinite selected transform renderer metric')
    return format(value, '.11g')


def apply(sections, sources, source, section, table, cite):
    receipt, passed = _state()
    source('S107', 'Basix transform-conditioned arithmetic and ray-domain acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/transform-ray-results.json',
           ('The selected gate passes 96 transform-ray fixtures, including 24 observed vertices and 24 edges, and six 96-case rejection controls. CPU 78 checks, reflected shader ABI and separately selected renderer/control acceptance remain distinct evidence. Representation enclosure and conditional arithmetic/hardware policies are not interchangeable.' if passed else 'Original per-triangle uncertainty/range implementation is under acceptance. Exact-real representation enclosure, conditional arithmetic policies and actual hardware observations are distinct evidence classes. No final selected gate is promoted while the owning harness contract is pending.'),
           kind='measured scoped correctness and conditional policy' if passed else 'implementation awaiting canonical acceptance',
           rights='Original Basix implementation and selected sanitized evidence MIT; private raw logs/telemetry excluded.')
    source('S108', 'Physically Based Rendering: Managing Rounding Error',
           'https://pbr-book.org/4ed/Shapes/Managing_Rounding_Error',
           'The textbook develops arithmetic error bounds for ray intersections and conservative origin placement. Excessive offsets can miss nearby detail, motivating explicit bounds and scope rather than a universal fixed epsilon. Its arithmetic analyses do not certify an unspecified Vulkan hardware implementation.',
           rights='Primary textbook linked and paraphrased; no source implementation copied.')
    source('S109', 'Vulkan specification: Ray Traversal',
           'https://docs.vulkan.org/spec/latest/chapters/raytraversal.html',
           'Traversal transforms rays into invertible instance space. Intersection determination is implementation specific and may introduce floating-point inaccuracies. The watertightness discussion concerns shared edges/vertices within an instance under identical transforms; it does not supply a universal transform/intersector error constant.',
           rights='Official specification linked and paraphrased; no implementation copied.')
    by = {s['id']: s for s in sections}
    body = '''<h3 id="transform-uncertainty">Transform uncertainty · arithmetic enclosure and conditional policies</h3>
<div class="callout"><strong>One certified arithmetic term does not certify hardware traversal</strong><p>The new opt-in path separates the exact-real representation term from three conditional FP32/intersector policy terms. A CPU-chosen inverse is an analysis input; it is not the inverse secretly used by hardware. Finite input checks and a passing local ray catalog cannot turn that policy into a Vulkan-wide precision guarantee.</p></div>''' + cite('S107', 'S109')
    body += '''<p><code>transform_uncertainty::analyze(uploaded_mesh, instances, domain)</code> accepts mesh vertices already quantized to uploaded FP32 and returns one five-vector budget per world triangle. Outward-rounded double intervals support the arithmetic analysis; every resulting component rounds upward to FP32. The representation term encloses the declared exact-real vertex-representation model. Forward, object-space and inverse terms remain conditional policies assuming normal finite FP32 intermediates or exact zero. Rejecting subnormal inputs alone does not establish that intermediate assumption.</p>'''
    body += table(['Budget / policy', 'Meaning and limit'], [
        ('representation', 'Deterministic exact-real representation-model enclosure from uploaded FP32 vertices and mapped world metadata.'),
        ('forward_policy', 'Conditional forward-transform FP32 allowance; cancellation-aware mapping retains absolute contributions rather than assuming a small result means small error.'),
        ('object_policy', 'Conditional object-space/intersector policy; per-axis extent is twice the maximum absolute edge component.'),
        ('inverse_policy', 'Conditional inverse/ray-domain allowance. The CPU inverse is not a hardware inverse certificate.'),
        ('total', 'Extra uncertainty supplied to world reconstruction, which must still add its own reconstruction error. This is not the final displacement.'),
        ('Condition / magnitude', 'Certified infinity-norm condition upper bound ≤1024 via an inverse-defect/Neumann argument; coefficient/object/world/inverse/inverse-translation magnitudes ≤2²⁰. Outward bounds may reject exact boundary values.'),
        ('Ray domain', 'Default origin limit 32 and maximum distance 128; maximum distance must be at least four times the origin limit. Scene vertices lie within origin_limit−1 per component.'),
        ('Budget cap', 'Each total extra-error component ≤minimum world altitude/64. This is an input-budget policy, not a guarantee about final offset size or distance to another triangle.')]) + cite('S107', 'S108')
    body += '''<figure><div style="overflow-x:auto"><svg viewBox="0 0 820 150" role="img" aria-labelledby="transform-budget-title" style="width:100%;min-width:690px;max-width:820px;height:auto"><title id="transform-budget-title">Arithmetic and hardware-policy evidence stay separate</title><g fill="none" stroke="currentColor" stroke-width="1.2"><rect x="6" y="16" width="205" height="116" rx="8"/><rect x="295" y="16" width="230" height="116" rx="8"/><rect x="610" y="16" width="204" height="116" rx="8"/><path d="M212 74 H288 M526 74 H602"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="13" text-anchor="middle"><text x="108" y="46">Uploaded FP32 mesh</text><text x="108" y="70">Validated matrices / domain</text><text x="108" y="94">finite · range · conditioning</text><text x="410" y="44">R: exact-real model enclosure</text><text x="410" y="70">F + O + I: conditional policies</text><text x="410" y="96">outward intervals → upward FP32</text><text x="712" y="44">Extra world uncertainty</text><text x="712" y="70">+ reconstruction error</text><text x="712" y="96">bounded hardware experiments</text></g></svg></div><figcaption>Arithmetic guarantees, chosen numerical policies and observed hardware behavior answer different questions.</figcaption></figure>
<h4>Explicit ray range and rejection</h4><p><code>--transform-rays</code> requires <code>--shared-instances --bounded-rays</code>. The shader checks origin components against 32, direction components against one and approximately unit squared norm, then clips ray distance to 128. Geometry is validated against the declared domain. Every accepted geometry vertex has component magnitude at most 31 and every accepted ray origin at most 32. A ray that reaches this geometry therefore travels at most √3 × 63 in world distance; with the accepted near-unit direction tolerance its parameter remains below 111, inside the 128 cap. This is a bounded-domain argument, not equivalence to an arbitrary unbounded scene. Near-parallel fixtures remain separate observations. No explicit beyond-cap visibility negative is claimed.</p>
<h4>Acceptance and quality tradeoff</h4><p><strong>Canonical acceptance pending.</strong> CPU arithmetic, actual GPU boundary/transform readbacks, near-parallel fixtures and domain/range refusals, and rendered image comparisons need separate frozen evidence. Root 208/Triangle 80 are the proposed new ABI; actual shader reflection and final harness checks remain required. Preliminary local observations are not selected completion evidence.</p>
<p>Extra uncertainty can move origins farther from a surface and alter paired image agreement. Image limits remain unchanged; a passing arithmetic/ray gate cannot erase that image-quality tradeoff or prove preservation of nearby detail. The new transform policy stays opt-in; the original default and earlier bounded-ray mode retain their separate acceptance.</p>
<p><strong>Next scope:</strong> finish this bounded uncertainty/range gate, then prioritize renderer depth/motion/material guide conventions and dynamic DLSS SR/reset/fallback. Cross-device or universal hardware proofs do not indefinitely block scoped guide preparation. Other M3 implementation, including RTXDI/many-light work, requires advance notice to the user. General asynchronous/multiqueue lifetime, device loss, asset importing, other devices and performance remain open.</p>''' + cite('S107', 'S108', 'S109')
    if passed:
        body = body.replace('<strong>Canonical acceptance pending.</strong> CPU arithmetic, actual GPU boundary/transform readbacks, near-parallel fixtures and domain/range refusals, and rendered image comparisons need separate frozen evidence. Root 208/Triangle 80 are the proposed new ABI; actual shader reflection and final harness checks remain required. Preliminary local observations are not selected completion evidence.',
            '<strong>Selected bounded gate passes.</strong> All 96 transform-ray fixtures pass, including 24 actual exact-vertex and 24 edge observations. Six rejection runs each reject 96 cases: zero/negative budgets, out-of-domain origins, invalid directions, omitted dispatch and an actually dispatched compiled no-op. An independent Decimal 90 oracle checks uploaded geometry, budgets, ray/target/blocker agreement and boundary coverage. The 78 CPU checks include 24 analytic forward/object/inverse anchors plus supported/refusal/forged-metadata/FP32-domain cases. Actual shader reflection verifies Root 208 with rayDomain@192, Triangle 80 with transformError@64 and unchanged Instance 16; independently compiled SPIR-V is byte-identical.')
        body = body.replace('finish this bounded uncertainty/range gate, then prioritize', 'preserve this bounded gate and prioritize')
        images = receipt['renderer_acceptance']['candidate']['positive_results']
        body += ('<p>The transform-policy renderer passes a ten-run suite with 48 positive probes, six rendered epochs and eight rejection controls. Worst global normalized RMSE is '
                 + _metric(images['candidate']['worst_normalized_rmse']) + ' at 16×16/256SPP and '
                 + _metric(images['larger-preview']['worst_normalized_rmse'])
                 + ' at 128×64/16SPP. These remain within the unchanged 0.05 global limit, but are larger than the earlier roughly 10⁻⁶ paired error. The separate control suite passes, and all three control phase HDR images are byte-identical to the published shared-v3 baseline. Passing image thresholds does not establish that the larger offset preserves arbitrary nearby detail.</p>') + cite('S107')
    if receipt:
        body += '<p><a href="transform-ray-results.json">Selected transform-ray receipt</a> · <a href="#roadmap">Scoped acceptance and remaining obligations</a></p>'
    by['reference-renderer']['body'] += body
    by['summary']['body'] += '<p><strong>Transform uncertainty:</strong> <a href="#transform-uncertainty">the arithmetic/range gate</a> ' + ('passes its selected bounded acceptance and ' if passed else 'remains under acceptance and ') + 'separates exact-real representation enclosure from conditional FP32 and hardware policies. No universal hardware error bound is claimed.</p>' + cite('S107')
    by['lessons']['body'] += '<p><strong>Name the guarantee:</strong> outward arithmetic can enclose a stated computation while a hardware intersector remains implementation specific. Cancellation, ray range, conditioning and intermediate normality require explicit scope. A triangle-altitude budget cap does not protect arbitrary nearby geometry; retain actual visibility and image tests.</p>' + cite('S107', 'S108', 'S109')


def enrich_library(library):
    receipt, passed = _state()
    library['summary'].append('Transform-conditioned uncertainty/range work separates deterministic representation enclosure from conditional FP32/hardware policies; ' + ('the selected bounded gate passes.' if passed else 'final acceptance is pending.'))
    library['findings'].append(dict(
        id='F24', title='Arithmetic enclosure and hardware traversal policy need separate acceptance',
        question_ids=['Q2'], topic='rendering',
        claim='Uploaded-FP32 representation error can be enclosed under an explicit real model; forward/object/inverse allowances remain conditional policies with finite range, conditioning and intermediate arithmetic assumptions.',
        evidence_type='measured scoped correctness and conditional policy' if passed else 'implementation awaiting canonical acceptance', confidence='high within stated scope' if passed else 'provisional',
        context='Original MIT opt-in transform uncertainty work; default domain 32/128; ' + ('selected 96-case/six-control gate, CPU 78 checks and separate renderer/control suites pass.' if passed else 'final selected acceptance pending.'),
        strengths='Explicit budget components, upward FP32 outputs, input-domain refusals and separate arithmetic versus hardware evidence.',
        limitations='No universal Vulkan intersector bound; chosen CPU inverse is not the hardware inverse. Normal intermediates, nearby-feature separation, other devices and performance remain unproved.',
        implications='Keep transform rays opt-in; require independent arithmetic, GPU range/visibility and unchanged image acceptance before scoped completion. Next M3 focus remains DLSS guides/dynamic SR.',
        tags=['Vulkan', 'rounding error', 'transform uncertainty', 'ray domain', 'conditional policy'],
        evidence=[dict(source_id=s, locator='Model, primary arithmetic reference or traversal specification', relation='supports', note='Arithmetic guarantees and hardware observations remain distinct.') for s in ['S107', 'S108', 'S109']]))
    library['scope_changes'].append('Added transform-conditioned arithmetic/range policy with explicit model/hardware limits. ' + ('Selected bounded acceptance completes its child only; general parent remains partial and defaults are unchanged.' if passed else 'No milestone completion or default-policy change is inferred.'))
