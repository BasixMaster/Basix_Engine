"""Scoped, opt-in triangle ray-spawn evidence; public receipts own acceptance."""
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
        raise ValueError('Expected a selected receipt object: ' + name)
    return value


def _state():
    receipt = _receipt('ray-spawn-results.json')
    progressive = _receipt('ray-progressive-results.json')
    candidate = receipt.get('results', {}).get('candidate', {}) if receipt else {}
    passed = bool(receipt and receipt.get('status') == 'passed'
                  and receipt.get('harness_checks_passed') is True
                  and candidate.get('status') == 'passed'
                  and candidate.get('bounded_rays') is True
                  and candidate.get('camera_relative_rebase') is True
                  and candidate.get('cases') == 180
                  and candidate.get('failed_cases') == 0)
    scene_pass = bool(progressive and progressive.get('status') == 'passed'
                      and progressive.get('bounded_rays') is True)
    return receipt, progressive, passed, scene_pass


def _number(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Invalid selected ray-spawn metric')
    return format(value, '.8g')


def apply(sections, sources, source, section, table, cite):
    receipt, progressive, passed, scene_pass = _state()
    source('S96', 'PBRT 4: Managing Rounding Error',
           'https://pbr-book.org/4ed/Shapes/Managing_Rounding_Error',
           'Floating-point hit positions can lie on either side of a surface. An offset that is too small causes false self-intersections; an offset that is too large skips nearby geometry. The chapter develops arithmetic error bounds and uses an uncertainty interval when spawning rays. Basix uses these mathematical principles in an original implementation, with hardware traversal uncertainty kept as an explicit, separate input.',
           kind='primary technical textbook',
           rights='Linked and paraphrased; no source implementation incorporated.')
    source('S97', 'NVIDIA: Solving Self-Intersection Artifacts in DirectX Raytracing',
           'https://developer.nvidia.com/blog/solving-self-intersection-artifacts-in-directx-raytracing/',
           'The analysis separates hit reconstruction, transforms and traversal errors, projects their uncertainty onto a geometric normal, and discusses offsetting both connection endpoints. Translating the working origin can reduce coordinate magnitudes. Its DXR/hardware assumptions are not a universal Vulkan bound: Basix independently implements the mathematics and treats its own identity-instance extent allowance as an empirical policy requiring device-specific acceptance.',
           kind='primary vendor numerical analysis',
           rights='Linked and summarized; no vendor source implementation or universal traversal constant adopted.')
    source('S98', 'Basix opt-in ray-spawn acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/ray-spawn-results.json',
           ('The canonical harness passes the scoped opt-in candidate and verifies expected rejection of legacy-offset, unrebased, omitted-dispatch, invalid-spawn and actual no-op-shader controls. The candidate checks 180 layered-triangle and synthetic short-connection fixtures. Actual uploaded gaps and numeric readbacks retain conversion limits.'
            if passed else 'The opt-in triangle error-box implementation is under evaluation. This documentation build has no passing selected canonical acceptance receipt; scratch results do not certify a completed gate.'),
           kind='local measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
           rights='Original Basix code, fixtures and selected numeric receipts MIT; no private telemetry included.')
    source('S99', 'Basix opt-in ray-spawn progressive integration',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/ray-progressive-results.json',
           ('The selected progressive-scene harness passes with bounded rays explicitly enabled. Scene images, accumulation/reset behavior and expected failures have a separate scope from layered-triangle numerical fixtures. The legacy offset policy remains the default.'
            if scene_pass else 'The opt-in ray-spawn candidate requires a separate progressive-scene integration receipt. Passing analytic layered-triangle fixtures alone does not establish rendered transport integration.'),
           kind='local measured scoped correctness' if scene_pass else 'implementation awaiting canonical acceptance',
           rights='Original Basix implementation and selected numeric receipts MIT.')
    by = {s['id']: s for s in sections}
    body = by['reference-renderer']['body']
    if passed:
        body = body.replace(
            'Thin gaps, large coordinates, transformed instances, imported/textured scenes, motion/depth guide conventions and dynamic DLSS recovery need their own acceptance.',
            'Thin-gap and large-coordinate cases now have a separately scoped opt-in experiment below. Hardware boundary hits, transformed instances, imported/textured scenes, motion/depth guide conventions and dynamic DLSS recovery remain open.')
    body += '''<h3 id="ray-spawn">Ray spawning · local coordinates and explicit uncertainty</h3>
<div class="callout"><strong>Opt-in experiment: <code>--bounded-rays</code></strong><p>The candidate reconstructs triangle hit points from hardware barycentrics and estimates FP32 arithmetic uncertainty. It offsets along the oriented geometric normal and rounds moved coordinates outward. The renderer's legacy offset policy remains the default. M2 and M2-gpu remain partial; this is scoped numerical evidence, with no universal Vulkan robustness claim.</p></div>
<p>A fixed epsilon can either leave a spawn point inside its source surface or move it beyond a nearby blocker. Basix separates the arithmetic error box from a caller-supplied traversal allowance. The helper projects the box onto the geometric normal. The current identity-instance experiment supplies an extent-dependent allowance based on <math xmlns="http://www.w3.org/1998/Math/MathML"><msub><mi>γ</mi><mn>7</mn></msub><mo>=</mo><mfrac><mrow><mn>7</mn><mi>ε</mi></mrow><mrow><mn>1</mn><mo>−</mo><mn>7</mn><mi>ε</mi></mrow></mfrac><mo>,</mo><mspace width=".4em"/><mi>ε</mi><mo>=</mo><msup><mn>2</mn><mrow><mo>−</mo><mn>24</mn></mrow></msup></math>. That traversal allowance is an empirical policy tested on the named RTX4090 configuration, rather than a proof for all hardware or instance transforms.</p>''' + cite('S96', 'S97') + '''
<figure><div style="overflow-x:auto"><svg viewBox="0 0 770 112" role="img" aria-labelledby="ray-flow-title ray-flow-desc" style="width:100%;min-width:650px;height:auto;max-width:770px"><title id="ray-flow-title">Ray-spawn coordinate and uncertainty pipeline</title><desc id="ray-flow-desc">Double-precision world positions are rebased before conversion to float. Barycentric triangle reconstruction supplies an arithmetic error box, with a separate traversal allowance, then both connection endpoints are offset.</desc><defs><marker id="ray-flow-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" markerHeight="5" orient="auto"><path d="M0 0 L10 5 L0 10Z" fill="currentColor"/></marker></defs><g fill="none" stroke="currentColor" stroke-width="1.3"><rect x="6" y="16" width="160" height="62" rx="8"/><rect x="204" y="16" width="160" height="62" rx="8"/><rect x="402" y="16" width="160" height="62" rx="8"/><rect x="600" y="16" width="164" height="62" rx="8"/><path d="M166 47 H197" marker-end="url(#ray-flow-arrow)"/><path d="M364 47 H395" marker-end="url(#ray-flow-arrow)"/><path d="M562 47 H593" marker-end="url(#ray-flow-arrow)"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="14" text-anchor="middle"><text x="86" y="40">Double world</text><text x="86" y="61">positions</text><text x="284" y="40">Subtract local origin</text><text x="284" y="61">before FP32 upload</text><text x="482" y="40">Triangle point</text><text x="482" y="61">and error box</text><text x="682" y="40">Offset both endpoints</text><text x="682" y="61">and bound the segment</text></g></svg></div><figcaption>Rebasing limits coordinate magnitude before float conversion. It cannot restore detail already rounded out of the original world representation.</figcaption></figure>
<p>Shadow connections offset both the receiver and sampled emitter point; the connection direction is recomputed and its endpoint interval shortened by one representable value. Invalid/nonfinite spawn data fails closed. A finite segment consumed by its uncertainty margin contributes zero direct lighting. Review caught both invalid data and an unusable interval being interpreted as visible; dedicated rejection and short-connection regressions cover those failure paths.</p>
<h4>Independent layered-triangle checks</h4>
<p>The fixture oracle checks known plane identity and the side/separation of the actual returned spawn position, rather than comparing two copies of the offset formula. It distinguishes a correct nearby hit from a self-hit and from a skipped blocker. The selected receipt records numeric hit/spawn readbacks and actual uploaded gaps for independent rechecking. CPU helper tests add 62 checks, including 12,000 independent double-precision plane/enclosure cases within the suite.</p>'''
    body += table(['Fixture domain', 'Scope and interpretation'], [
        ('Scales and separation', 'Triangle scales 0.001, 1 and 1000; nominal near-blocker gaps are 0.00001 times scale. The receipt also records the actual gap after translation, rebasing and FP32 conversion.'),
        ('Coordinate handling', 'Double-precision fixture translations 0, 2²⁰ and −2²⁴, followed by local-origin subtraction before upload. Double rounding can already change a tiny nominal gap; rebasing does not recover absent information.'),
        ('Directions and surfaces', 'Front/back spawning, normal and grazing secondary directions, plus straight and angled shadow connections. No transformed TLAS instance is implied.'),
        ('Five fixture kinds', 'Self-intersection miss, close secondary blocker, near-light shadow blocker, clear shadow connection, and synthetic short-connection conservative rejection. The final kind tests failure handling, not resolved visibility below the error budget.'),
        ('Acceptance limits', '180 fixtures: 72 secondary and 108 shadow/connection checks. Exact hardware edge/vertex barycentrics, arbitrary transforms, subnormal arithmetic and other GPUs remain unverified.'),
    ])
    if passed:
        body += '<p><strong>Canonical numerical gate passes:</strong> all 180 candidate fixtures pass. The six-case harness also confirms that the legacy-offset and unrebased controls fail their fixture gates, and that omitted dispatch, invalid spawn and an actually compiled no-op shader reject poisoned/invalid output. Whole-process core/SyncVal logs and frozen executable, shader, compiler, validation and source hashes are checked. A negative control failing as expected is evidence about the harness, not a passing implementation.</p>' + cite('S98')
        controls = [value for name, value in receipt.get('results', {}).items() if name != 'candidate']
        if controls and any('fixtures' not in value for value in controls):
            body += '<p>The public projection retains the complete candidate fixture rows and compact control summaries. Hashes identify the frozen full control receipts retained locally; the compact summaries do not expose every negative-control readback.</p>'
    else:
        body += '<p><strong>Canonical numerical acceptance pending:</strong> this documentation build has no passing selected ray-spawn harness receipt. Preliminary fixture runs remain development evidence.</p>' + cite('S98')
    if scene_pass:
        positive = [r for r in progressive.get('results', {}).values()
                    if isinstance(r, dict) and r.get('status') == 'passed']
        body += '<p><strong>Opt-in progressive integration passes:</strong> the selected scene harness explicitly enables bounded rays.'
        if positive:
            body += (' Worst measured global normalized RMSE is ' + _number(max(r['normalized_rmse'] for r in positive))
                     + ', low-radiance normalized RMSE ' + _number(max(r['low_radiance_normalized_rmse'] for r in positive))
                     + ', and channel-mean relative error ' + _number(max(r['max_channel_mean_relative_error'] for r in positive)) + '.')
        body += ' These are fixed-scene CPU/GPU correctness results; they establish no real-time or cross-device performance result.</p>' + cite('S99')
    else:
        body += '<p><strong>Progressive integration acceptance pending:</strong> a separate selected scene receipt must verify the opt-in candidate. Numerical fixtures alone do not certify transport integration.</p>' + cite('S99')
    body += '''<p><strong>Next acceptance:</strong> exercise hardware edge/vertex hit barycentrics, varied triangle orientations and transformed instances before broader adoption. Then extend imported geometry, depth/motion conventions and temporal DLSS reset/recovery. Keep the legacy NDF sampling discrepancy and the new ray-origin experiment as separate numerical contracts.</p>'''
    links = ['<a href="#roadmap">Milestones and transition log</a>']
    if receipt:
        links.append('<a href="ray-spawn-results.json">Scoped ray-spawn harness</a>')
    if progressive:
        links.append('<a href="ray-progressive-results.json">Opt-in progressive integration</a>')
    body += '<p>' + ' · '.join(links) + '</p>'
    by['reference-renderer']['body'] = body
    status = 'passes its selected scoped numerical gate' if passed else 'awaits selected canonical numerical acceptance'
    by['summary']['body'] += '<p><strong>Ray-origin update:</strong> the opt-in triangle error-box candidate ' + status + '. The working origin is removed before FP32 upload; actual converted separations remain part of the evidence. Legacy offsets remain the renderer default. <a href="#ray-spawn">Fixtures, error policy and remaining limits</a>.</p>' + cite('S98')
    by['lessons']['body'] += '<p><strong>Visibility needs a valid interval:</strong> a nonfinite spawn or an interval consumed by endpoint uncertainty cannot be treated as an unobstructed connection. Test failure handling as well as geometric identity. Record the separation that actually reaches the GPU: translating and rebasing a tiny feature can change it even in double precision. An empirical traversal allowance needs named device/geometry limits.</p>' + cite('S98')


def enrich_library(library):
    receipt, progressive, passed, scene_pass = _state()
    library['summary'].append('Opt-in triangle error-box ray spawning '
                             + ('passes its scoped canonical fixture gate' if passed else 'awaits canonical fixture acceptance')
                             + '; legacy offsets remain the default and broader M2 is partial.')
    library['findings'].append(dict(
        id='F21', title='Local coordinates and explicit uncertainty require separate geometric and failure-path acceptance',
        question_ids=['Q2'], topic='rendering',
        claim='Barycentric reconstruction, an arithmetic error box and a separate empirical traversal allowance support a scoped opt-in spawn policy. Layered planes independently check hit identity and converted separation; unusable connections must fail closed.',
        evidence_type='measured scoped correctness' if passed else 'implementation awaiting canonical acceptance',
        confidence='high' if passed else 'provisional',
        context='2026-10-28; original MIT CPU/Slang implementation; identity TLAS instance on the named Windows RTX4090/NVIDIA617.42 configuration.',
        strengths='Independent plane/side oracles, actual numeric readbacks and uploaded gaps, front/back and angled cases, explicit control failures and invalid/unusable interval regressions.',
        limitations='Three scales and translations, layered triangles and a synthetic short-connection regression. The traversal allowance is empirical. Hardware edge/vertex barycentrics, arbitrary transforms, subnormal arithmetic, other GPUs and performance remain open.',
        implications='Keep --bounded-rays opt-in. Extend hardware boundary and transformed-instance tests before broad adoption; then imported geometry and temporal guides require separate evidence.',
        tags=['Vulkan', 'ray origin', 'rounding error', 'camera relative', 'shadow rays', 'independent oracle'],
        evidence=[dict(source_id=s, locator='Numerical method, selected fixture and integration receipts', relation='supports',
                       note='Arithmetic analysis, empirical traversal allowance and executed acceptance have distinct scopes.')
                  for s in ['S96', 'S97', 'S98', 'S99']]))
    library['actions'][0].update(
        title='Extend ray-spawn boundaries, scene and temporal acceptance', finding_ids=['F19', 'F20', 'F21'],
        rationale='The selected VNDF proposal and opt-in ray-origin experiment have distinct contracts; neither closes broader geometry or temporal coverage.',
        success_criterion='Ray-spawn fixture and opt-in scene receipts pass; hardware edge/vertex and transformed-instance gates then pass before broader adoption. Import/textures and depth/motion/reset behavior remain separate subsequent acceptance.')
    library['scope_changes'].append('Added original opt-in triangle error-box spawning, camera-relative fixture conversion and scoped layered-triangle/connection acceptance. Actual uploaded gaps, failed controls and failure-path corrections are retained; empirical traversal policy is not promoted to a universal Vulkan bound.')
