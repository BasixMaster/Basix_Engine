"""Visible-GGX sampled-state proposal and independent acceptance evidence.

Reads selected public receipts only; private scratch results never certify a gate.
The legacy NDF result is retained as a separate historical control.
"""
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
        raise ValueError('Expected an object in ' + name)
    return value


def _state():
    visible = _receipt('visible-results.json')
    progressive = _receipt('visible-progressive-results.json')
    sampling_pass = bool(visible and visible.get('status') == 'passed')
    scene_pass = bool(progressive and progressive.get('status') == 'passed')
    adopted = (sampling_pass and scene_pass and visible.get('adopted_as_gpu_default') is True
               and progressive.get('default_sampler_verified') is True
               and progressive.get('sampler') == 'vndf')
    return visible, progressive, sampling_pass, scene_pass, adopted


def _number(value):
    if not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Nonfinite or missing selected VNDF result')
    return format(value, '.8g')


def _fraction(numerator, denominator):
    return '<math xmlns="http://www.w3.org/1998/Math/MathML" display="inline"><mfrac><mrow>' + numerator + '</mrow><mrow>' + denominator + '</mrow></mfrac></math>'


def apply(sections, sources, source, section, table, cite):
    visible, progressive, sampling_pass, scene_pass, adopted = _state()
    source('S92', 'Heitz: Sampling the GGX Distribution of Visible Normals',
           'https://jcgt.org/published/0007/04/01/',
           'Projected-disk construction for visible GGX normals. Basix implements the published mathematics independently in original MIT CPU/Slang code. The visible-normal proposal changes sample probabilities; it does not add multiple-scattering energy compensation or change the existing separable Smith BRDF.',
           kind='primary research paper',
           rights='Paper linked and summarized; no paper implementation copied into Basix.')
    source('S93', 'PBRT 4: Conductor BRDF sample state',
           'https://pbr-book.org/4ed/Reflection_Models/Conductor_BRDF',
           'The conductor sampling discussion evaluates a sample using its generated microfacet normal. Standalone BRDF/PDF evaluation reconstructs that normal from outgoing and incoming directions. These are mathematically related operations whose floating-point conditioning can differ. Basix uses the distinction as a numerical design reference, with its own material model and code.',
           kind='primary technical textbook', rights='Linked and paraphrased; no source implementation incorporated.')
    source('S94', 'Basix visible-GGX sampling acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/visible-results.json',
           ('Canonical visible-GGX harness passes: four deterministic 4096-case streams, independent nine-configuration GPU distribution/null/energy checks, and deliberately faulty output/PDF controls. This receipt concerns the distinct sampled-state proposal; the legacy NDF failure remains separate.'
            if sampling_pass else 'The distinct visible-GGX sampled-state implementation is under evaluation. No passing canonical sampling receipt is available to this documentation build; preliminary scratch runs are not promoted to published acceptance.'),
           kind='local measured scoped correctness' if sampling_pass else 'implementation awaiting canonical acceptance',
           rights='Original Basix code and selected numeric receipts MIT; no vendor runtime included.')
    source('S95', 'Basix visible-GGX progressive PT acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/visible-progressive-results.json',
           ('Canonical fixed-scene progressive harness passes with the visible-GGX proposal, matched 24-bit CPU streams, independent HDR decoding, accumulation/reset checks and expected rejection controls. No scene import, temporal reconstruction or UE-detail benchmark is established.'
            if scene_pass else 'Visible-GGX renderer integration awaits its separate canonical progressive-scene acceptance receipt. A passing material sampler alone does not establish transport integration.'),
           kind='local measured scoped correctness' if scene_pass else 'implementation awaiting canonical acceptance',
           rights='Original Basix code, original fixture and selected numeric receipts MIT.')
    by = {s['id']: s for s in sections}
    body = by['reference-renderer']['body']
    body = body.replace('CPU reference tests now total76 checks.', 'The CPU reference suite also checks sampler selection, default preservation and invalid-proposal rejection.')
    body = body.replace('<strong>Current next gate:</strong> resolve grazing sampling conditioning and independently validate GPU distributions/energy, then add import and depth/motion conventions.',
                        '<strong>Current numerical work:</strong> a distinct visible-GGX proposal is evaluated below; the original NDF failure remains a separate control. Import and depth/motion conventions still require implementation.')
    body = body.replace('<h3>Sampling numerical gate · partial, with the original failure retained</h3>',
                        '<h3>Legacy NDF numerical gate · partial, with the original failure retained</h3>')
    body = body.replace('Accordingly, M2-gpu remains partial despite the fixed-scene result. Robust grazing arithmetic and independent GPU sampling-distribution/energy checks are next.',
                        'The original NDF gate remains partial. The later visible-GGX proposal below has a separate identity and acceptance contract; it cannot make that retained result pass. M2-gpu remains partial while broader renderer acceptance is open.')
    body += '''<h3 id="visible-ggx">Visible-GGX v1 · keep the generated normal</h3>
<p>The new proposal samples microfacet normals visible from the outgoing direction. It keeps the generated half vector and its projection while computing the sample's BRDF and PDF. Accepted near-unit directions are normalized explicitly. Null reflections below the surface stay null; no rejection loop changes their probability. The single-scatter, separable Smith BRDF and supported roughness range are unchanged. CPU reference rendering retains its legacy sampler default; the new proposal is selected explicitly with <code>--sampler vndf</code>.</p>''' + cite('S92', 'S93') + '''
<figure><div style="overflow-x:auto"><svg viewBox="0 0 760 168" role="img" aria-labelledby="vndf-flow-title vndf-flow-desc" style="width:100%;min-width:640px;height:auto;max-width:760px"><title id="vndf-flow-title">Two numerical evaluation paths</title><desc id="vndf-flow-desc">A generated microfacet normal supplies the sampled-state BRDF and PDF. A separately rounded direction can reconstruct a different normal near grazing and is diagnosed separately.</desc><defs><marker id="vndf-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10Z" fill="currentColor"/></marker></defs><g fill="none" stroke="currentColor" stroke-width="1.3"><rect x="8" y="15" width="185" height="52" rx="8"/><rect x="281" y="15" width="213" height="52" rx="8"/><rect x="563" y="15" width="188" height="52" rx="8"/><rect x="281" y="103" width="213" height="52" rx="8"/><rect x="563" y="103" width="188" height="52" rx="8"/><path d="M193 41 H274 M494 41 H556 M101 67 V129 H274 M494 129 H556" marker-end="url(#vndf-arrow)"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="14" text-anchor="middle"><text x="100" y="37">Generated normal</text><text x="100" y="56">and reflection</text><text x="387" y="37">Retained normal state</text><text x="387" y="56">sample f and PDF</text><text x="657" y="37">Transport acceptance</text><text x="657" y="56">plus independent energy</text><text x="387" y="125">Rounded direction</text><text x="387" y="144">reconstruct normal</text><text x="657" y="125">Conditioning diagnostic</text><text x="657" y="144">separate contract</text></g></svg></div><figcaption>Sample state and standalone evaluation answer related numerical questions. Neither diagnostic replaces the retained legacy oracle.</figcaption></figure>
<p>At extreme grazing, the sum of outgoing and incoming directions nearly cancels. Rounding the stored direction to FP32 can then change the reconstructed normal enough to change a narrow GGX PDF. More precise trigonometry alone cannot recover information already lost in direction storage. The new sampled-state contract avoids that reconstruction for the generated sample. Standalone evaluation at the rounded direction remains a reported conditioning diagnostic, with no claim that it clears the legacy same-variates criterion.</p>
<p><strong>Arithmetic lesson:</strong> the first VNDF probe exposed a separate disk-rim cancellation in the projected hemisphere height. Basix now factors the difference of squares and rationalizes its small factor. The exact-real disk mapping, input variates, roughness domain and declared conformance tolerances stay the same. The first failed run is retained locally; additional deterministic streams check the revised arithmetic beyond that failure.</p>
<h3>Independent probability and white-environment energy</h3>
<p>The GPU distribution executable does not link or call the CPU material implementation. Its targets integrate projected-NDF half-slope radius with analytic azimuth arcs, and integrate physical BRDF energy after cancelling the distribution Jacobian. It checks 16 equal-z intervals across four sign quadrants, plus a null bin: 65 bins in total. Nine configurations cover normal/grazing Lambert, roughness 1 and 0.05 white conductors, two oblique conductors, and one white dielectric mixture. Nulls contribute zero energy and stay in the population denominator.</p>'''
    body += table(['Independent analytic anchor', 'Non-null probability', 'White-environment reflected energy'], [
        ('Lambert with unit albedo', '1', '1'),
        ('White GGX conductor, alpha = 1; z is outgoing cosine',
         _fraction('<mn>1</mn>', '<mn>1</mn><mo>+</mo><mi>z</mi>'),
         _fraction('<mn>2</mn><mo>(</mo><mn>1</mn><mo>−</mo><mi>ln</mi><mn>2</mn><mo>)</mo>', '<mn>1</mn><mo>+</mo><mi>z</mi>')),
    ])
    body += '''<p>Histogram and energy limits use a Bonferroni allocation of Bernstein bounds with family alpha 10<sup>−6</sup>, assuming independent pseudorandom variates. Explicit FP32 floors are 0.00002 for probability and 0.0002 for normalized energy. Quadrature refinement envelopes must remain below 0.00002; these are convergence estimates, not rigorous interval bounds. White-conductor/Lambert normalized weights have a 1.0003 bound, while the single dielectric-mixture test uses the declared conservative bound 4. These fixtures do not prove all mixed-material energy behavior.</p>
<p>Separate CPU tests cover 67 analytic, quadrature, reciprocity, state, invalid-input and weight-bound checks. The distribution oracle also checks its analytic endpoints and deliberately biased synthetic populations. Canonical GPU acceptance additionally requires no-op shaders to fail and an actual shader with a 10% PDF increase to fail the energy gate while leaving its direction histogram unchanged.</p>'''
    if sampling_pass:
        body += '<div class="callout"><strong>Canonical sampling gate passes</strong><p>Four deterministic streams each pass all 4,096 conformance cases at the declared tolerances, including three holdout seeds. The independent GPU gate passes nine configurations with 262,144 samples each: 2,359,296 samples. The biased-PDF shader fails all 27 energy-channel checks while preserving the sampled histogram. The three actual shader fault controls confirm that successful dispatch and plausible directions alone do not establish correct transport weights.</p></div>' + cite('S94')
    else:
        body += '<div class="callout"><strong>Canonical sampling acceptance pending</strong><p>The implementation and independent oracle exist, but this build has no passing selected harness receipt. Preliminary development runs are not published acceptance.</p></div>' + cite('S94')
    if scene_pass:
        results = progressive.get('results', {})
        positive = [r for r in results.values() if isinstance(r, dict) and r.get('status') == 'passed']
        if positive:
            global_error = max(r['normalized_rmse'] for r in positive)
            low_error = max(r['low_radiance_normalized_rmse'] for r in positive)
            mean_error = max(r['max_channel_mean_relative_error'] for r in positive)
            body += '<p><strong>Visible-GGX scene integration passes:</strong> worst measured global normalized RMSE ' + _number(global_error) + ', low-radiance normalized RMSE ' + _number(low_error) + ', and channel-mean relative error ' + _number(mean_error) + '. These are scoped fixed-scene CPU/GPU agreement results, with the original limits 0.05, 0.10 and 0.02 respectively.</p>' + cite('S95')
        else:
            body += '<p>The selected progressive-scene harness reports a pass for the visible-GGX proposal. Its receipt defines the tested variants and metric scope.</p>' + cite('S95')
    else:
        body += '<p><strong>Renderer integration acceptance is pending.</strong> Sampling acceptance and a CPU test cannot substitute for the separate progressive GPU scene gate.</p>' + cite('S95')
    if adopted:
        body += '<p><strong>Adoption decision:</strong> visible-GGX v1 is the GPU progressive renderer default after its sampling and scene gates pass. <code>--sampler ndf</code> retains the explicit legacy control. The CPU renderer default remains unchanged.</p>'
    body += '''<p><strong>Remaining gates:</strong> M2 and M2-gpu remain partial. The retained legacy NDF contract is unresolved. Thin gaps, large coordinates, transformed instances, imported/textured scenes, motion/depth guide conventions and dynamic DLSS recovery need their own acceptance. Ordinary triangle RT and passing VNDF checks establish no CLAS, cooperative-matrix, neural reconstruction, UE-detail or cross-device performance result.</p>
<p><a href="visible-results.json">Visible-GGX sampling harness</a> · <a href="visible-progressive-results.json">Visible-GGX scene harness</a> · <a href="sampling-results.json">Retained legacy partial receipt</a> · <a href="#roadmap">Current milestones and transition log</a></p>'''
    by['reference-renderer']['body'] = body
    by['summary']['body'] = by['summary']['body'].replace('M2 remains partial while GPU PT/import/motion are pending.', 'M2 remains partial while broader GPU transport, import and motion acceptance are open.')
    by['summary']['body'] = by['summary']['body'].replace('the ordinary Vulkan progressive scene gate passes; M2 remains partial because18 extreme grazing sampling cases still fail the original paired criterion.', 'the ordinary Vulkan progressive scene gate passes. The legacy NDF control retains 18 paired grazing failures; a separate visible-GGX proposal and its independent acceptance are described below. M2 remains partial.')
    status = 'passes its canonical sampling and progressive-scene gates' if sampling_pass and scene_pass else 'is undergoing separate canonical sampling and progressive-scene acceptance'
    by['summary']['body'] += '<p><strong>Visible-GGX update:</strong> the distinct sampled-state proposal ' + status + '. The original NDF failure is preserved, and remaining scene/temporal gates stay open. <a href="#visible-ggx">Numerical contract, independent physics checks and limits</a>.</p>' + cite('S92', 'S94', 'S95')
    by['lessons']['body'] += '<p><strong>Retain useful sample state:</strong> near-cancelling directions can discard information needed to reconstruct a sharp microfacet PDF in FP32. A distinct visible-normal proposal can retain the generated normal, but it needs independent probability, null-mass and energy tests. Agreement between matching implementations alone is insufficient. Factoring unstable expressions preserves a mathematical mapping more faithfully than introducing an unmeasured cutoff.</p>' + cite('S92', 'S93', 'S94')


def enrich_library(library):
    visible, progressive, sampling_pass, scene_pass, adopted = _state()
    status = 'passes canonical sampling and fixed-scene transport gates' if sampling_pass and scene_pass else 'awaits complete canonical sampling and fixed-scene transport acceptance'
    library['summary'].append('Distinct visible-GGX sampled-state proposal ' + status + '; legacy NDF grazing failures remain recorded and broader M2 stays partial.')
    library['findings'].append(dict(
        id='F20', title='Generated microfacet state and independent physics checks define a distinct sampling gate',
        question_ids=['Q2'], topic='rendering',
        claim='The visible-GGX proposal retains its generated half vector for sampled f/PDF. An independent radial/azimuth oracle checks GPU directional/null probabilities and physical reflected energy; this is distinct from the retained legacy NDF criterion.',
        evidence_type='measured scoped correctness' if sampling_pass else 'implementation awaiting canonical acceptance',
        confidence='high' if sampling_pass else 'provisional',
        context='2026-10-28; original MIT CPU/Slang implementation; Windows RTX4090/NVIDIA617.42 is the tested GPU configuration.',
        strengths='Analytic Lambert/alpha1 anchors, separate host probability/energy integration, retained failed controls, fixed tolerances, held-out streams and explicit proposal identity.',
        limitations='Nine GPU distribution configurations; confidence bounds assume iid pseudorandom variates and numerical envelopes are refinement estimates. Legacy NDF conditioning is unresolved. No universal mixed-material, imported-scene, temporal or performance result.',
        implications='Keep legacy and visible-normal contracts separate; only adopt after the selected sampling and scene gates pass. Continue thin-gap/coordinate and scene/guide acceptance while keeping M2 partial.',
        tags=['Vulkan', 'GGX', 'VNDF', 'sampling', 'numerical conditioning', 'independent oracle'],
        evidence=[dict(source_id=s, locator='Algorithm explanation, scope and selected acceptance receipt', relation='supports', note='Mathematics, implementation and measured acceptance are separate evidence types.') for s in ['S92', 'S93', 'S94', 'S95']]))
    # Earlier evidence is a dated baseline, not a claim that no later GPU oracle exists.
    for finding in library['findings']:
        if finding['id'] == 'F19':
            finding['title'] = 'Earlier GPU scene result retained a failing legacy grazing control'
            finding['context'] += ' Historical NDF baseline; later visible-GGX evidence is F20.'
            finding['implications'] = 'Retain the original partial NDF receipt. Evaluate the distinct visible-GGX contract through F20 without rewriting the old oracle result; broader M2 remains partial.'
    library['actions'][0].update(
        title='Complete renderer scene and temporal gates', finding_ids=['F19', 'F20'],
        rationale='Visible-GGX has a distinct numerical contract; its acceptance does not close the legacy NDF result or broader renderer integration.',
        success_criterion='Selected VNDF sampling/distribution and progressive-scene receipts pass before adoption; thin-gap/large-coordinate, import/textures and depth/motion/reset gates then pass independently.')
    library['scope_changes'].append('Added original visible-GGX sampled-state proposal, factored disk-rim arithmetic and independent GPU probability/null/energy acceptance. Legacy NDF failure and CPU default are preserved; no advanced Vulkan extension or performance claim is promoted.')
