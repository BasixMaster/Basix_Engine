"""Receipt-owned authored-scene guides and bounded dynamic SR lifecycle docs."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import json
import math

DOC = Path(__file__).resolve().parents[1] / 'docs'
PINNED_GUIDE = 'https://github.com/NVIDIA-RTX/Streamline/blob/625db07d4bca5abc67d781c42af7e7fb0761f1b6/docs/ProgrammingGuideDLSS.md'


def _state(name):
    path = DOC / name
    if not path.is_file():
        return None, False
    value = json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(value, dict):
        raise ValueError('Expected selected DLSS/guide receipt object')
    results, numeric, cases = value.get('results', {}), value.get('independent_numeric', {}), value.get('cases', [])
    candidate = results.get('candidate', {})
    abi = value.get('abi_reflection', {})
    types = abi.get('types', {})
    passed = (value.get('status') == 'passed' and value.get('harness_checks_passed') is True
              and value.get('input_hashes_unchanged') is True
              and value.get('loader_confirmed_validation') is True
              and value.get('cpu_checks') == 110
              and abi.get('status') == 'passed'
              and types.get('GuideRoot', {}).get('bytes') == 128
              and types.get('GuidePixel', {}).get('bytes') == 96
              and types.get('UploadRoot', {}).get('bytes') == 64
              and types.get('reconstruction_guides', {}).get('compiled_shader_byte_identity') is True
              and types.get('reconstruction_upload', {}).get('compiled_shader_byte_identity') is True
              and all(case.get('expected_outcome_verified') is True for case in cases))
    if name == 'reconstruction-guides-results.json':
        names = ('candidate', 'fault-negate-motion', 'fault-double-motion', 'fault-constant-depth',
                 'fault-camera-only-motion', 'fault-omit', 'compiled-no-op')
        passed = (passed and len(cases) == 7
                  and {case.get('name') for case in cases} == set(names)
                  and all(k in results and k in numeric for k in names)
                  and value.get('synchronization_validation_requested') is True
                  and candidate.get('status') == 'passed' and candidate.get('frames') == 8
                  and candidate.get('pixels') == 20304 and candidate.get('failed_pixels') == 0
                  and candidate.get('hits') == 4017 and candidate.get('misses') == 16287
                  and candidate.get('valid_motions') == 1632
                  and numeric.get('candidate', {}).get('failed_pixels') == 0
                  and all(numeric[k].get('pixels') == 20304
                          and numeric[k].get('numeric_readback_rechecked') is True
                          and results[k].get('validation_enabled') is True
                          and results[k].get('validation_errors') == 0
                          and results[k].get('validation_warnings') == 0 for k in names)
                  and all(results[k].get('failed_pixels', 0) > 0
                          and numeric[k].get('failed_pixels', 0) > 0 for k in names[1:])
                  and all(results[k].get('failed_pixels') == 20304
                          and numeric[k].get('failed_pixels') == 20304
                          for k in ('fault-omit', 'compiled-no-op')))
    else:
        names = {'candidate', 'nonprojectable-history-reset', 'forced-fallback', 'omitted-evaluation',
                 'post-evaluation-cleanup', 'shutdown-retains-native', 'release-candidate', 'release-refuses-validation'}
        fallback, recovery = results.get('forced-fallback', {}), results.get('nonprojectable-history-reset', {})
        passed = (passed and len(cases) == 8 and {case.get('name') for case in cases} == names
                  and value.get('debug_synchronization_validation_requested') is True
                  and value.get('runtime_version') == '2.14.16'
                  and candidate.get('frames') == 11 and candidate.get('sdk_evaluations') == 10
                  and candidate.get('fallback_frames') == 1 and candidate.get('feature_frees') == 3
                  and candidate.get('reset_evaluations') == 4
                  and recovery.get('reset_evaluations') == 5
                  and recovery.get('nonprojectable_history_resets') == 1
                  and numeric.get('nonprojectable-history-reset', {}).get('injected_nonprojectable_history') is True
                  and fallback.get('sdk_loaded') is False and fallback.get('sdk_evaluations') == 0
                  and fallback.get('fallback_frames') == 11
                  and results.get('release-candidate', {}).get('validation_enabled') is False
                  and all(results.get(k, {}).get('status') == 'passed'
                          and results[k].get('frames') == 11
                          and results[k].get('validation_errors') == 0
                          and results[k].get('validation_warnings') == 0
                          and results[k].get('sdk_teardown_errors') == 0
                          and numeric.get(k, {}).get('numeric_readback_rechecked') is True
                          for k in ('candidate', 'nonprojectable-history-reset', 'forced-fallback', 'release-candidate'))
                  and all(case.get('exit_code') == (1 if case['name'] in
                          {'omitted-evaluation', 'post-evaluation-cleanup', 'shutdown-retains-native', 'release-refuses-validation'} else 0)
                          for case in cases))
    return value, bool(passed)


def _metric(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError('Missing/nonfinite selected DLSS metric')
    return format(value, '.8g')


def apply(sections, sources, source, section, table, cite):
    guides, guides_pass = _state('reconstruction-guides-results.json')
    renderer, renderer_pass = _state('dlss-renderer-results.json')
    source('S110', 'Basix authored-scene GPU reconstruction guide acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/reconstruction-guides-results.json',
           ('The selected seven-run gate passes eight guide frames and 20,304 pixels with independent geometry/projection/raw-readback checks. 110 CPU checks and reflected 128/96/64 ABI pass; six expected fault/no-op runs reject. This establishes bounded guide correctness, not SDK temporal reconstruction quality.' if guides_pass else 'Actual GPU guide generation is under canonical acceptance. An independent scene/projection oracle checks depth, camera/object motion, material data, clipping and raw guide readback. Guide correctness is distinct from SDK temporal reconstruction quality.'),
           kind='measured scoped correctness' if guides_pass else 'implementation awaiting canonical acceptance', rights='Original Basix math, shader and adapters MIT; private raw logs/telemetry excluded.')
    source('S111', 'Basix moving renderer to optional DLSS SR lifecycle acceptance',
           'https://github.com/BasixMaster/Basix_Engine/blob/main/docs/dlss-renderer-results.json',
           ('The selected eight-case gate passes actual Debug/Release moving-renderer SR, input/output resize, reset/toggle, nonprojectable-history recovery and SDK-absent fallback, including independent raw conversion checks and lifecycle negatives. Temporal/detail quality, PT denoising, RR and FG remain unaccepted.' if renderer_pass else 'Moving renderer integration, input/output resize, resets, toggles and fallback are under canonical acceptance. Actual SDK evaluation and conversion/lifetime correctness do not alone establish temporal/detail quality, PT denoising, Ray Reconstruction or Frame Generation.'),
           kind='measured scoped correctness' if renderer_pass else 'implementation awaiting canonical acceptance', rights='Original adapter MIT; separately supplied Streamline/DLSS runtime retains its own terms and is not vendored.')
    source('S112', 'Pinned Streamline 2.14.16 DLSS integration guide', PINNED_GUIDE,
           'The pinned guide specifies adapter support checks, optimal input sizing, resource tags and extents, per-frame options/constants, reset and lifecycle responsibilities. Required SR resources include render-resolution color, depth and motion plus output color. Basix independently validates its numerical conventions and actual tagged buffers; API presence does not certify reconstruction quality.',
           rights='Official pinned programming guide linked and paraphrased; no SDK implementation or runtime binaries copied.')
    by = {s['id']: s for s in sections}
    by['nographics']['body'] += '<h3 id="backend-responsibilities">Current implementation responsibilities</h3><p>NoGraphicsAPI is the Vulkan device/resource/submission foundation of the current renderer. Basix adds native Vulkan adapters for acceleration structures, ray-query shader pipelines and optional Streamline interoperation. The current PT and guide path is a mixed implementation; no percentage of engine work or claim that every GPU allocation/dispatch passes through NoGraphicsAPI is supported.</p>' + table(['Responsibility', 'Current owner / implementation'], [
        ('Device and submission ownership', 'NoGraphicsAPI creates the Vulkan device, command pools and timeline semaphores; Basix owns the session/lifetime policy and uses its submission/completion API.'),
        ('General shader data and textures', 'NoGraphicsAPI heaps, textures and texture/sampler descriptor heaps hold the pointer-root data and native image resources used by the adapters.'),
        ('BLAS / TLAS / build scratch', 'Basix native Vulkan acceleration-structure adapter creates these objects and native buffers/memory, including vkAllocateMemory. These allocations are not all NoGraphicsAPI heap allocations.'),
        ('PT and guide compute pipelines', 'Basix NativePipeline creates Vulkan pipeline/layout/AS descriptor bindings, records vkCmdBindPipeline/vkCmdBindDescriptorSets and actual vkCmdDispatch on the borrowed native command buffer. NoGraphicsAPI owns that command buffer’s pool/submission lifetime.'),
        ('Upload, readback and fallback compute', 'NoGraphicsAPI compute/resource operations are used for these stages; this does not make the separate native RT/guide dispatch its compute-pipeline API.'),
        ('Optional DLSS', 'Original Basix adapters expose the same device/native Vulkan image and command metadata to Streamline. NVIDIA supplies the separately licensed SDK implementation/runtime.'),
        ('Rendering algorithms and checks', 'Original Basix materials, PT sampling, world metadata, authored orientation, error policies, guide math and independent acceptance tools. NoGraphicsAPI supplies backend primitives rather than these algorithms.')]) + '<p>Implementation audit: <a href="https://github.com/BasixMaster/Basix_Engine/blob/main/src/progressive_gpu.cpp">owner, native buffers and NativePipeline</a> · <a href="https://github.com/BasixMaster/Basix_Engine/blob/main/src/dlss_renderer_main.inl">guide dispatch and renderer/SDK conversion</a>. This describes the current Vulkan path; future backend refactoring remains a separate decision.</p>'
    by['architecture']['body'] += '<p><strong>Current backend boundary:</strong> NoGraphicsAPI owns the Vulkan device/resource/submission foundation; Basix native Vulkan adapters perform AS allocation/builds, PT/guide pipeline binding and dispatch, and optional SDK interoperation. See <a href="#backend-responsibilities">the responsibility table</a> for the implemented split.</p>'
    body = '''<h3 id="reconstruction-guides">Reconstruction guides · conventions before SDK evaluation</h3>
<div class="callout"><strong>Projection correctness and temporal quality are different gates</strong><p>The authored-scene guide path generates actual depth, camera/object motion and material metadata on the GPU. A separate CPU projection/world oracle checks the meaning of each value. SDK tags, finite buffers and a stable screenshot cannot establish the convention or demonstrate temporal reconstruction quality.</p></div>''' + cite('S110', 'S112')
    body += table(['Guide / convention', 'Bounded contract'], [
        ('View and pixel axes', 'Top-left origin, +X right, +Y down; pixel centers at (x+½,y+½), view +forward. Display aspect is explicit and stays fixed when rounded input extents change.'),
        ('Depth', 'Positive linear view depth and noninverted perspective depth: near 0, far 1. Primary rays clip to the declared near/far range; diagnostic projections outside it do not promise clamped depth.'),
        ('Jitter', 'Jitter means projected image displacement, so primary rays sample pixel center minus projection jitter. Motion vectors exclude current and previous jitter.'),
        ('Motion', 'Current-to-previous displacement in current render-pixel units. Across input-size changes, previous normalized UV is measured in current pixels. SDK normalization uses scale(1/width,1/height).'),
        ('Object history', 'The previous point is the previous location of the same rigid material surface point, not the current world point. Camera-only matrices cannot replace object motion.'),
        ('Offscreen history', 'Finite previous projections outside the viewport remain natural displacement. No FLT_MAX sentinel is used. Nonprojectable foreground history triggers a whole-frame reset where SR consumes no per-pixel mask.'),
        ('Miss/background', 'Constant background, far depth1 and zero motion. These authored-scene choices are explicit; they do not generalize to an animated environment.'),
        ('ABI', 'Separate GuideRoot 128 and GuidePixel 96; the progressive PT Root 208 remains unchanged. Material/albedo/normal/roughness data is generated and checked, but SR does not consume every future RR guide.')]) + cite('S110')
    body += '''<p>The dependency-free CPU guide API independently defines projection, unjittered reprojection and history validity. GPU guides reuse the renderer transport conventions while a frozen world/pixel oracle checks raw 96-byte records. Acceptance includes hit/miss counts, object motion, input-size/aspect changes and near/far clipping; known motion-sign/scale, constant-depth, camera-only and omitted/no-op faults must reject. A valid motion mask establishes projection, not previous visibility or disocclusion.</p>
<p><strong>Guide canonical acceptance pending:</strong> implementation and preliminary runs do not complete the guide milestone. No new selected guide result is promoted until the owning seven-run harness and CPU/ABI evidence are frozen.</p>''' + cite('S110')
    body += '''<h3 id="dlss-renderer">Moving renderer → native images → optional DLSS SR</h3>
<figure><div style="overflow-x:auto"><svg viewBox="0 0 820 142" role="img" aria-labelledby="dlss-flow-title" style="width:100%;min-width:700px;max-width:820px;height:auto"><title id="dlss-flow-title">Same-device renderer inputs and optional SDK output flow</title><g fill="none" stroke="currentColor" stroke-width="1.2"><rect x="5" y="22" width="181" height="94" rx="8"/><rect x="225" y="22" width="169" height="94" rx="8"/><rect x="433" y="22" width="169" height="94" rx="8"/><rect x="641" y="22" width="174" height="94" rx="8"/><path d="M186 69 H225 M394 69 H433 M602 69 H641"/></g><g fill="currentColor" font-family="system-ui,sans-serif" font-size="13" text-anchor="middle"><text x="95" y="50">GPU renderer color</text><text x="95" y="73">+ depth / motion guides</text><text x="95" y="96">original MIT generation</text><text x="309" y="50">Native Vulkan images</text><text x="309" y="73">same owned device</text><text x="309" y="96">no CPU color re-upload</text><text x="517" y="50">Optional SDK SR</text><text x="517" y="73">or original bilinear</text><text x="517" y="96">explicit reset / lifetime</text><text x="728" y="50">Native / engine readback</text><text x="728" y="73">independent conversion</text><text x="728" y="96">temporal quality separate</text></g></svg></div><figcaption>Actual data movement and SDK lifecycle form a bounded integration gate; temporal detail requires additional image-sequence acceptance.</figcaption></figure>''' + cite('S111', 'S112')
    body += table(['Sequence event', 'Required behavior'], [
        ('Moving frames', 'Actual renderer radiance and current depth/motion are converted on the GPU into native images on the NoGraphicsAPI-owned Vulkan device.'),
        ('Dynamic input size', 'Use supported SDK input bounds and tagged extents while retaining display projection aspect; reproject motion through normalized previous UV.'),
        ('Camera cut / output resize', 'Reset incompatible history and recreate/free feature/resources in a documented order after readers retire.'),
        ('Off / on', 'Run original bilinear fallback when disabled; re-enable with a reset rather than consuming incompatible SDK history.'),
        ('No runtime', 'Forced fallback must work with a nonexistent SDK path without loading runtime DLLs. SDK binaries remain optional and separately licensed.'),
        ('Output checking', 'Check independent GPU-input conversion, native/engine/HDR equality and every fallback RGB. SDK output variation demonstrates liveness, not an independent high-quality target.')])
    body += '''<p><strong>Dynamic renderer canonical acceptance pending:</strong> preliminary actual SDK frames and forced-fallback runs are implementation evidence only. The final gate must retain precise event/frame metadata, full-process validation, reviewed minimal-runtime warnings and fault controls through teardown. An exploratory run with a broader SDK directory emitted ignored-plugin warnings; it is not the selected clean-runtime acceptance.</p>
<p><strong>Limits and next work:</strong> SR is an upscaler, not the PT denoiser. These guides and lifecycle tests do not implement RR/FG, prove disocclusion/ghosting/detail quality, remove input Monte Carlo noise or establish performance/cross-device behavior. Preserve a non-SDK path and keep optional SDK terms separate from the MIT engine. The next quality gate needs fixed reference sequences and predeclared temporal/detail criteria. Non-DLSS M3 work, including RTXDI/many-light integration, remains pending and requires advance notice to the user.</p>''' + cite('S111', 'S112')
    if guides:
        body += '<p><a href="reconstruction-guides-results.json">Guide acceptance receipt</a></p>'
    if renderer:
        body += '<p><a href="dlss-renderer-results.json">Renderer/SDK lifecycle receipt</a></p>'
    if guides_pass:
        result = guides['results']['candidate']
        body = body.replace('<strong>Guide canonical acceptance pending:</strong> implementation and preliminary runs do not complete the guide milestone. No new selected guide result is promoted until the owning seven-run harness and CPU/ABI evidence are frozen.',
            '<strong>Selected guide gate passes:</strong> the seven-run harness checks eight actual frames and 20,304 pixels: 4,017 hits, 16,287 misses and 1,632 valid motions. All candidate pixels pass independent raw guide checks. Motion-negation/scaling, constant-depth and camera-only faults reject; omitted and actually dispatched compiled no-op controls each reject all 20,304 pixels. The eighth frame exercises near 6/far 7 clipping. 110 CPU checks and actual reflection verify GuideRoot 128, GuidePixel 96 and UploadRoot 64, with independently compiled shader byte identity.')
    if renderer_pass:
        result = renderer['results']['candidate']
        body = body.replace('<strong>Dynamic renderer canonical acceptance pending:</strong> preliminary actual SDK frames and forced-fallback runs are implementation evidence only. The final gate must retain precise event/frame metadata, full-process validation, reviewed minimal-runtime warnings and fault controls through teardown. An exploratory run with a broader SDK directory emitted ignored-plugin warnings; it is not the selected clean-runtime acceptance.',
            '<strong>Selected lifecycle gate passes:</strong> eight cases include an 11-frame sequence with 10 actual SDK evaluations and one original bilinear fallback, reproduced in Debug and Release. Input size changes at frame 4, camera cut/reset at 5, output resize at 7, off at 8 and on at 9. Three feature frees and four reset evaluations are observed. An injected nonprojectable foreground-history case proves the pre-reset condition from raw guides and causes one additional whole-frame reset. A nonexistent-runtime path loads no SDK DLL and checks all 11 bilinear frames. Omitted evaluation, cleanup/shutdown retention and Release validation misuse are required rejections.')
        body += '<p>The selected renderer independently checks ' + str(result['guide_pixel_checks']) + ' guide pixels, ' + str(result['input_component_checks']) + ' input components and ' + str(result['output_component_checks']) + ' output components per SDK profile. Worst measured depth error is ' + _metric(result['worst_depth_error']) + ' and motion error is ' + _metric(result['worst_motion_error_pixels']) + ' render pixels. Debug core/SyncVal reports zero errors/unreviewed warnings; SDK teardown errors are zero. Three exact pinned manual-hook warnings remain explicitly reviewed. Release runs without validation, and its validation request refuses rather than pretending validation is active.</p>' + cite('S111')
        body += '<p>The first Release trial failed because an available optional debug-utils extension was disabled. Enabling that available extension fixed the strict SDK run; the warning whitelist was not relaxed. The broader exploratory SDK-directory ignored-plugin warnings are also retained as a first-failure lesson. Canonical acceptance uses the existing reviewed minimal runtime with identical pins.</p>' + cite('S111')
        if (DOC / 'assets/dlss-renderer-sequence.png').is_file():
            body += '<figure><img src="assets/dlss-renderer-sequence.png" alt="Selected moving-renderer SR lifecycle frames including camera and object motion, cut, resize, fallback and history restart" loading="lazy" style="width:100%;height:auto"><figcaption>Measured canonical lifecycle frames at 16 shading samples per pixel. Primary rays use the guide-aligned pixel center minus projection jitter. The contact sheet illustrates actual frame events and output liveness; it is not temporal/detail-quality, PT denoising or performance acceptance.</figcaption></figure>' + cite('S111')
    by['reference-renderer']['body'] += body
    by['summary']['body'] += '<p><strong>DLSS-only continuation:</strong> <a href="#reconstruction-guides">actual authored-scene guides</a> and <a href="#dlss-renderer">moving renderer/SDK lifecycle</a> ' + ('pass their selected bounded gates' if guides_pass and renderer_pass else 'are under separate acceptance') + '. Temporal/detail quality, RR and FG remain separate.</p>' + cite('S110', 'S111')
    by['lessons']['body'] += '<p><strong>Keep semantic and quality oracles separate:</strong> wrong jitter sign, motion units or material-point history can produce plausible finite buffers. Check raw projection/reprojection first, then SDK resource/lifecycle behavior, then temporal image quality. A passing SR evaluation is not evidence that noisy PT has been denoised.</p>' + cite('S110', 'S111', 'S112')


def enrich_library(library):
    _, guide_pass = _state('reconstruction-guides-results.json')
    _, renderer_pass = _state('dlss-renderer-results.json')
    library['summary'].append('Authored-scene guide and dynamic optional SR lifecycle ' + ('pass selected bounded acceptance' if guide_pass and renderer_pass else 'are under separate canonical acceptance') + '; temporal/detail quality and RR/FG remain unaccepted.')
    for id, title, claim, ids in [
        ('F25', 'Depth and motion guides need independent projection semantics', 'Pixel axes, jitter, current-to-previous sign/units, previous rigid surface points and clipped depth must be checked independently of SDK tags and finite output.', ['S110', 'S112']),
        ('F26', 'Actual SR lifecycle is distinct from temporal PT reconstruction quality', 'Same-device GPU renderer input, native images, resets/resize/toggles and optional fallback establish integration behavior; output liveness does not establish denoising or temporal/detail quality.', ['S111', 'S112'])]:
        passed = guide_pass if id == 'F25' else renderer_pass
        library['findings'].append(dict(id=id, title=title, question_ids=['Q2'], topic='rendering', claim=claim,
            evidence_type='measured scoped correctness' if passed else 'implementation awaiting canonical acceptance', confidence='high within stated scope' if passed else 'provisional',
            context='Original MIT authored-scene renderer/guide adapter; optional pinned SDK has separate terms.',
            strengths='Independent scene/projection/raw-buffer checks; explicit guide convention, lifecycle and no-runtime fallback boundaries.',
            limitations=('Bounded authored scene and one GPU; ' if passed else 'Canonical results pending; ') + 'no temporal/detail quality, PT denoising, RR/FG, cross-device or performance acceptance.',
            implications=('Preserve guide/lifecycle gates and define' if passed else 'Finish separate guide/lifecycle receipts, then define') + ' independent temporal/detail sequence criteria. Keep non-DLSS M3 pending.',
            tags=['DLSS', 'motion vectors', 'depth', 'reprojection', 'Vulkan', 'fallback'],
            evidence=[dict(source_id=s, locator='Selected authored-scene evidence or pinned SDK programming contract', relation='supports', note='Numerical correctness, API lifecycle and reconstruction quality are distinct.') for s in ids]))
    library['scope_changes'].append('Added DLSS-only authored-scene guide and renderer-lifecycle work with ' + ('selected bounded passing gates' if guide_pass and renderer_pass else 'separate pending canonical gates') + '. No SDK quality or non-DLSS M3 completion inferred.')
