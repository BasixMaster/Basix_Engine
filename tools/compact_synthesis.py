"""Compact synthesis of existing evidence; no new experiments."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def apply(sections, sources, source, section, table, cite):
    receipts = [json.loads((ROOT / 'docs' / name).read_text(encoding='utf-8')) for name in ('progressive-results.json', 'reconstruction-guides-results.json', 'dlss-renderer-results.json')]
    if not all(d.get('status') == 'passed' for d in receipts):
        raise ValueError('Selected measured summary requires passing receipts')
    p, g, d = [r['results'][key] for r, key in zip(receipts, ('main', 'candidate', 'candidate'))]
    by = {s['id']: s for s in sections}
    by['summary']['title'] = 'Research in brief: conclusions, evidence and next step'
    by['summary']['body'] = '''<p class="lead">Build a Vulkan-first, MIT engine around NoGraphicsAPI, with an explicit native ray-tracing/SDK adapter. The foundation works in bounded tests; UE-like content scale and reconstructed image quality remain goals.</p>
<p class="doc-caption">Synthesis updated 30 October 2026 · research and rendering evidence through 28 October · detailed chapters and status logbook follow.</p>
<h3 id="compact-conclusions">Five conclusions</h3><ol>
<li><strong>NoGraphicsAPI is the foundation:</strong> device, heaps, textures, commands and submission. Basix supplies native Vulkan AS/pipelines/dispatch, rendering and DLSS interoperability. A code/effort percentage would be misleading.</li>
<li><strong>UE-like detail needs coordinated systems:</strong> LOD/residency, materials, lighting and reconstruction. MegaLights addresses stochastic many-light direct illumination, not full PT. RTXDI/RTXPT are references, not a ready-made MIT engine.</li>
<li><strong>Geometry techniques have separate roles:</strong> CLAS/templates reuse geometry-build work; PTLAS updates changing instance regions; tetrahedral cages deform reused detail; DGF/DGFS compress geometry. LinAlg/cooperative matrices accelerate math. Each needs independent capability and acceptance gates.</li>
<li><strong>Optimize the reconstructed frame:</strong> account for AS maintenance, tracing, denoising/upscaling and VRAM together. SER, OMM, caches and material LOD are workload-dependent. Vendor speedups do not predict Basix results; deliberate bias needs a reference comparison.</li>
<li><strong>Keep licenses explicit:</strong> original engine code and NoGraphicsAPI can remain MIT; optional NVIDIA SDKs, papers and game artwork retain separate terms. PETAFAB supports specialization, not general engine or PT parity.</li></ol>''' + cite('S01', 'S02', 'S09', 'S25', 'S26', 'S113', 'S114') + '''
<figure><picture><source media="(max-width:680px)" srcset="assets/engine-ownership-mobile.svg"><img src="assets/engine-ownership.svg" alt="NoGraphicsAPI provides GPU resources and commands; the Basix native Vulkan adapter supplies RT and SDK interoperability; original Basix systems implement rendering. All share one device and explicit lifetime contracts." style="width:100%;height:auto"></picture><figcaption>Architecture and ownership, not code-volume proportions. Advanced geometry and many-light paths remain separate roadmap work.</figcaption></figure>
<h3 id="compact-measured-results">What the existing experiments establish</h3><p>Local 28 October results: RTX 4090 / NVIDIA 617.42. These are different bounded tests, not a combined score.</p>''' + table(['Existing result', 'Measured evidence', 'Limit'], [
        ('Progressive hardware PT', f"Matched CPU/GPU: {p['width']}×{p['height']}, {p['spp']:,} samples/pixel; normalized RMSE {p['normalized_rmse']:.2e}. NEE/MIS and accumulation/reset checks pass.", 'Small authored scene with matched random streams; not UE parity or a frame-rate benchmark.'),
        ('GPU depth/motion guides', f"{g['frames']} frames, {g['pixels']:,} pixels, {g['failed_pixels']} failed. Independent projection and rigid material-point history checks.", 'Guide correctness, not reconstruction quality. Diagnostic material guides are not accepted RR inputs.'),
        ('Actual DLSS SR', f"{d['frames']} frames: {d['sdk_evaluations']} SDK evaluations + {d['fallback_frames']} fallback; {d['guide_pixel_checks']:,} guide checks. Worst motion error {d['worst_motion_error_pixels']:.2e} render pixels.", 'Resize/reset/toggle and resource handling pass; temporal/detail quality, PT denoising, RR and FG remain separate.')]) + cite('S91', 'S110', 'S111') + '''
<div class="synthesis-results"><figure><img src="assets/progressive-cornell.png" alt="Actual Basix progressive GPU path-traced authored box scene"><figcaption>Existing 256×256 GPU PT preview; no matched CPU-image acceptance at this larger size. The numerical result above is a separate 32×32 case.</figcaption></figure><figure><img src="assets/dlss-renderer-sequence.png" alt="Existing authored DLSS sequence contact sheet"><figcaption>Existing 16 shading-sample/pixel SR sequence. Output/lifecycle evidence; retained noise is not denoising or temporal-quality acceptance. <a href="#dlss-renderer">Full context</a>.</figcaption></figure></div>
<p><strong>Numerical lesson:</strong> robust ray origins and transform budgets need independent tests. Representation-error enclosure and conditional hardware allowances are different claims; the opt-in transform path changes measured image error. No universal watertightness or cross-GPU proof follows.</p>''' + cite('S107') + '''
<h3 id="compact-next-step">Next step and uncertainty</h3><p><strong>Define DLSS temporal/detail-quality acceptance next:</strong> reference/ROI sequences for disocclusion, thin moving geometry, glossy motion and lighting changes. Set metrics and limits before experiments. Keep non-DLSS M3 work pending and notify the user before starting it.</p>
<p>CLAS/PTLAS, cages, many lights and matrix acceleration retain separate gates. The 28 October DX12 1.722 release establishes preview availability, not a tested Basix path; SM6.10 remains preview in that evidence snapshot. No new simulation, benchmark or driver upgrade was performed for this synthesis. <a href="#roadmap">Roadmap</a> · <a href="#nvidia-whitepapers">Whitepapers</a> · <a href="#sources">Sources</a>.</p>''' + cite('S115', 'S117')
    old = by['lessons']['body']
    by['lessons']['body'] = '''<p><strong>Separate availability, integration correctness, visual quality and performance.</strong> Each needs independent evidence.</p><ul>
<li>Prove depth, jitter and motion semantics before evaluating neural reconstruction quality.</li>
<li>Preserve CPU references, frozen inputs and failure controls: plausible output is not sufficient proof of correct execution.</li>
<li>Choose optimizations by measured bottleneck and total frame cost; topology, partitioning, compression and sampling solve different problems.</li>
<li>Keep small-scene, one-GPU acceptance scoped; production scalability and UE parity remain unproven.</li></ul>
<p><a href="#summary">Compact results and next step</a> · <a href="#roadmap">Acceptance gates and history</a>.</p>
<details><summary>Detailed lessons and experiment-specific qualifications</summary>''' + old + '</details><p class="doc-caption">Changelog · 2026-10-30: compact synthesis and ownership diagram; existing measured rendering results reused without new experiments.</p>'

def enrich_library(library):
    library['updated_on'] = '2026-10-30'
    library['compact_synthesis'] = dict(updated_on='2026-10-30', evidence_as_of='2026-10-28', anchor='summary', next_step='Define M3-dlss-temporal-quality reference/ROI sequences and thresholds.', measurements=['progressive-results.json', 'reconstruction-guides-results.json', 'dlss-renderer-results.json'], limits='No new experiments, driver update or implementation promotion.')
    library['scope_changes'].append('30 October: compact evidence-led synthesis and original ownership diagram; reused existing outputs and receipts with original dates and limits.')
