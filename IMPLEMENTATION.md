# Basix implementation foundation

This is original MIT experimental code, not a complete game engine. The default build is a dependency-free C++20 CPU correctness target. The optional Vulkan experiment uses a separately prepared, pinned MIT NoGraphicsAPI copy; it does not download dependencies at configure time.

## CPU cage reference

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`basix_cage` implements one affine tetrahedral cell, barycentric mapping, unnormalized ray transforms preserving parameter t, conservative bounds/cell clipping, inverse-transpose normals and invalid/inverted-cell rejection. Its 78 checks passed with MSVC 19.44 on 28 October 2026. This is not cage generation, triangle preprocessing, a GPU cage BVH or watertight traversal. The default relative tolerance is a Basix experiment policy, not a threshold endorsed by the paper.

## Vulkan / NoGraphicsAPI experiment

Install a suitable C++20 compiler, CMake 3.24+, Vulkan SDK headers/libs 1.4.357+ and Slang 2026.14.1+ (tested 2026.18.2). The tested revisions and archive digest are in `dependencies.lock.json`. Read all dependency notices before redistributing a build.

Clone NoGraphicsAPI separately at `ffd9f2ab6b7442dcfbe0cb0051d84d0edb078d89`, then prepare a **new, separate destination**:

```text
python tools/prepare_nographicsapi.py --source PATH_TO_UPSTREAM --destination PATH_TO_PREPARED_COPY
cmake -S . -B build-gpu -DBASIX_BUILD_GPU_SPIKE=ON -DBASIX_NOGRAPHICSAPI_SOURCE=PATH_TO_PREPARED_COPY -DBASIX_SLANGC=PATH_TO_SLANGC
cmake --build build-gpu --config Release
ctest --test-dir build-gpu -C Release --output-on-failure
```

The preparation tool verifies the exact clean upstream revision and refuses to overwrite an existing destination. It copies the upstream tree and applies an original experimental Vulkan-only extension: explicit ray-query device initialization and borrowed device/physical-device/command-buffer handles and controlled native texture metadata. The upstream checkout stays unchanged. This extension is not an upstream-supported API or a Metal backend implementation.

Only committed bytes are exported (`git archive`), so ignored local files cannot enter the prepared dependency. GPU CMake validates the exact revision marker and tracks the included shader ABI/compiler as build dependencies. Apple is explicitly excluded from this Vulkan-only spike; the CPU target remains portable. RT eligibility is currently checked after upstream adapter selection, so a multi-GPU machine may reject the selected adapter even if another could run RT; explicit adapter selection remains open.

The test owns its device through NoGraphicsAPI. It dispatches a NoGraphicsAPI pointer-root compute shader, then builds ordinary Vulkan BLAS/TLAS objects and executes ray queries on that **same device**. Native Vulkan code is isolated in the experiment; native pipelines use a separate command-recording scope. Submissions are completed before pool reset or resource destruction; the lifecycle test deliberately holds a real submission behind a host-signaled timeline gate. Static and translated triangle results are compared with an independent analytic CPU oracle over two 64Ã—64 grids.

Run `basix_gpu_spike --output DIRECTORY` for PPM images and a JSON result. The directory is explicit and receives only generated experiment files. Unsupported initialization fails rather than silently substituting software results. No DLSS binary, ray pipeline, DXR2/CLAS implementation or advanced rendering is included. The texture experiment uses registry-issued monotonic borrow tickets, expected metadata checks and timeline-gated retirement. Only own RGBA8 2D single-mip images on the same device/queue are covered.

## Scoped M1 validation and texture checks

Prepare a new dependency destination with the current tool. GPU CMake now requires interop patch version5; stale prepared copies fail explicitly. Compile Debug and Release, optionally configuring `-DBASIX_VALIDATION_TESTS=ON` for the Debug-only CTest case.

Use a separately installed or locally extracted Khronos validation package. The checked Windows nightly archive and DLL digests are in `dependencies.lock.json`; the mutable nightly URL does not promise future availability of those exact bytes. The layers are testing tools, not vendored/shipped engine runtime. No machine-wide loader setting is changed.

```text
python tools/run_gpu_validation.py --exe build-gpu/Debug/basix_gpu_spike.exe --release-exe build-gpu/Release/basix_gpu_spike.exe --validation-layer-dir PATH_TO_CHECKED_LAYERS --output work/validation
```

The helper sets `VK_LAYER_PATH`, disables implicit third-party overlays only for its child process, and requests core plus synchronization validation with `VK_VALIDATION_VALIDATE_SYNC=1`. It verifies the pinned DLL hash and loader trace, scans full-process logs, and checks three positive cycles plus four expected-failure cases. Full logs remain in the selected output directory; the sanitized receipt contains no local paths/account telemetry.

The GPU experiment writes a NoGraphicsAPI storage texture, transitions GENERALâ†’TRANSFER_SRC for a native copy, returns to GENERAL, runs a native storage-image compute pass, then explicitly restores NoGraphicsAPI heap/PSO bindings before engine readback. Tests recreate64Ã—64,17Ã—9 and1Ã—1 offscreen images. Borrow tickets capture their epoch privately; epochs do not reset between cycles. Wrong metadata, stale tickets and premature retirement are rejected before native API use.

`--validate` refuses an unvalidated Release build. `--cycles 1..16` bounds resource churn. `--shader-dir` selects a shader resource directory and supports failure-path testing. Standalone C++ diagnostic counters cover the messenger lifetime after device initialization and before device teardown; the helper separately scans startup and shutdown logs. Zero validation messages are evidence for the named workload, not a proof covering every GPU hazard.

## Current verified scope

- CPU:78 independent cage-math checks passed; no GPU cage/preprocessing claim.
- Initial RTX4090/NVIDIA617.42:4096 pointer-root pixels and8192 hardware rays passed.
- Reviewed M1 Debug/core/SyncVal harness:5cases;3positive cycles;12288 pointer pixels,24576 rays,25500 exact texture pixel checks,63 metadata/stale-ticket/retirement rejections. Zero warning/error logs including startup/shutdown. Layer manifest reports1.4.366; checked archive/DLL hashes are pinned.
- Expected failures: invalid cycle count, missing-shader cleanup, image-write-failure cleanup, and Release refusal to claim validation.
- CPU/docs CI passed on Linux/Windows for prior foundation commit923d10b in run40677723983. Current changes receive their own CI after publication; GPU tests remain local physical-device checks.

## Remaining gates

The authoritative states, evidence and next tests are in `roadmap.json`; `ENGINE_STATUS.md` and the documentation roadmap table/logbook are generated from it. M1 validation, controlled native-texture, repeated-device/in-flight lifecycle and actual static SR subgates pass for the named local configuration. Parent M1 remains partial. Temporal SDK quality and device-loss recovery are pending; hidden-window resizing/zero-size restoration pass but actual OS minimize remains untested.

Full PT, production DLSS integration, geometry streaming, cage generation/clipping/GPU traversal and clustered-AS backends are still pending. No independent spirv-val, cross-vendor GPU validation or performance benchmark is claimed. Offscreen fixture size changes are separate from the hidden-window test. Public CI runs offline CPU/roadmap/docs checks and needs no GPU SDK downloads.

## Repeated device sessions and real pending retirement

Interop patch3 adds a narrow host timeline-signal helper. Each texture submission waits for an initially unsignaled gate. The fixture marks the borrow in flight, queries the actual completion timeline, verifies it is below the submitted serial, and rejects retirement with that real value. It releases the gate, waits, checks the pixels, then retires. A scoped gate drains work before dependent resources are destroyed, including the injected exception path. No pool reset occurs while its commands are pending.

Private registry-incarnation cookies complement per-image epochs. Two fresh registries with identical device/image/extent/epoch metadata reject each other's tickets, so recycled native handles cannot impersonate another registry session. Device sessions are destroyed before the success receipt is written; the wrapper scans complete startup/shutdown logs.

```text
python tools/run_gpu_validation.py --exe build-gpu/Debug/basix_gpu_spike.exe --release-exe build-gpu/Release/basix_gpu_spike.exe --validation-layer-dir PATH_TO_CHECKED_LAYERS --sessions 3 --output work/lifecycle
python tools/run_window_validation.py --executable build-gpu/Debug/basix_window_smoke.exe --layer-dir PATH_TO_CHECKED_LAYERS --output work/window
```

The seven-case lifecycle harness passed36864 pointer pixels,73728 ray checks,76500 texture comparisons,189 contract rejections and27 registry-incarnation checks across3devicesÃ—3cycles. Of the contract rejections,27 use truly incomplete GPU submissions. This is bounded correctness, not concurrent frames, multiqueue support or a performance benchmark. See `docs/lifecycle-results.json`.

## Hidden Win32 swapchain test

The WIN32-only console target creates a never-visible, offscreen popup. It presents8clear frames across64Ã—64,320Ã—180,17Ã—9 extents and a96Ã—54 restoration; three acquisitions at a real0Ã—0 client extent return empty. The four-process wrapper also tests controlled exceptions before device creation, after creation and after a completed present. All windows are destroyed and full-process validation logs are clean. Receipts are in `docs/window-results.json`.

The first run exposed an upstream swapchain format/usage issue: sRGB plus STORAGE usage was unsupported on the tested adapter (VUID01778/02275). The clear-only test uses supported `bgra8_unorm`; validation was retained. Original failure logs stay in the workbench. This does not prove sRGB presentation support, displayed pixels, OS minimize, device loss or SDK correctness.

## Actual optional DLSS SR gate

This Windows-only target uses separately supplied Streamline2.14.16 headers and signed production runtime modules. It is off by default. The lock file pins the official SDK archive, selected header hashes and six runtime component hashes. No SDK source, binary or model is vendored, and default CPU/GPU checks do not download it.

After obtaining and verifying the pinned official package, prepare a new NoGraphicsAPI patch5 destination and stage the runtime outside the Basix repository:

```text
python tools/prepare_streamline_runtime.py --sdk-root PATH_TO_STREAMLINE --destination ../work/streamline-runtime
cmake -S . -B build-gpu -DBASIX_BUILD_GPU_SPIKE=ON -DBASIX_NOGRAPHICSAPI_SOURCE=PATH_TO_PATCH5 -DBASIX_BUILD_DLSS_SPIKE=ON -DBASIX_STREAMLINE_SDK=PATH_TO_STREAMLINE
cmake --build build-gpu --config Debug
cmake --build build-gpu --config Release
python tools/run_dlss_validation.py --exe build-gpu/Debug/basix_dlss_spike.exe --release-exe build-gpu/Release/basix_dlss_spike.exe --runtime ../work/streamline-runtime --validation-layer-dir PATH_TO_CHECKED_LAYERS --output work/dlss-validation --check-known-driver-bug
```

The stage helper copies checked `sl.interposer`, `sl.common`, `sl.dlss`, `sl.pcl`, `NvLowLatencyVk` and `nvngx_dlss` DLLs plus separate notices. PCL is automatically added by the SDK. The original MIT loader verifies the interposer's Authenticode/NVIDIA signer with cached trust data and locks it against replacement while loaded. The optional SDK's own terms still apply. Disabled OTA/downloaded-plugin options do not guarantee offline operation: the observed SDK invokes an NGX bootstrap updater.

Basix initializes Streamline before Vulkan, uses its own stable custom-engine ProjectID GUID/version, queries required extensions/features, then registers the NoGraphicsAPI-owned device. No NVIDIA-issued application ID was needed for this local evaluation. Unknown feature names or missing support fail closed. The SDK's returned lists omitted privateData and the core1.4 pushDescriptor bit needed by observed calls; initial full-process validation exposed these requirements. Basix queries both before enabling them and normalizes the conflicting obsolete EXT buffer-device-address request to core1.2/KHR support.

The actual SR fixture supplies RGBA16F color/output, R32F depth, RG32F zero motion, explicit1x1 exposure, full native metadata and camera/frame constants. Three frames upscale427x240 to640x360. All922320 input RGB components match the independent constant oracle;2073600 output RGB components match the independent native FP16 copy exactly and the expected radiance within the predeclared RMSE<0.03/maxerror<0.15 tolerance. Measured RMSE0.000347785 and maxerror0.00146484. Output and both readback memories are reset each frame, so stale/no-op execution cannot pass.

The first actual evaluation returned success with black pixels. This reproduces [NVIDIA issue109](https://github.com/NVIDIA-RTX/Streamline/issues/109): active descriptor heaps prevent the tested SR runtime reading input images. Binding an empty conventional descriptor set immediately before evaluation fixes this named workload. Afterward Basix re-emits both native heaps, pipeline and pointer-root data in the same command buffer. The no-handoff control still fails the image oracle. This is an explicit temporary SR compatibility adapter; no FG compatibility follows.

Eight harness cases cover actual evaluation, omitted-evaluation rejection, cleanup after a real evaluation, unavailable extension rejection, relative runtime rejection, invalid signature rejection, Release validation refusal and the known-driver-bug control. Whole-process Vulkan/core/SyncVal logs have zero warnings/errors; three SDK warnings about unavailable automatic state-tracking hooks are reviewed separately because the host restores state manually. Feature free and SDK shutdown are checked before success; if SDK shutdown fails, still-live modules/device objects are retained until process exit instead of unsafe unloading. See `docs/dlss-results.json` for the sanitized receipt. Raw SDK logs, initial failures and images stay in the workbench.

This is a static fixed-resolution interoperability gate on Windows/RTX4090/NVIDIA617.42, not a temporal/detail-quality or performance benchmark. No motion/jitter quality, SDK resize, camera-cut/fallback sequence, RR, FG, cross-vendor support or actual device-loss recovery is claimed. `M1b-sdk` is completed only for this named scope; `M3-dlss` and parentM1 remain partial.

## Milestone and status logbook

`roadmap.json` retains the original history and dated structured transitions with item ID, previous/new status, reason and evidence. The HTML roadmap and generated Markdown show the same logbook. Validation rejects broken transition chains, current states that contradict the latest recorded transition, unknown states/items and escaping/missing evidence paths. Research coverage and an existing adapter never substitute for executed engine acceptance tests.

## Materials and CPU progressive reference (M2 partial)

Default builds now include `basix_material`, `basix_reference` and `basix_reference_render`. CPU tests contain70 material checks and79 transport checks. Stable IDs, analytic spheres/triangles, one-sided triangle emitters, NEE, power MIS, compensated RR and deterministic accumulation form an original MIT oracle. The GGX model is single scatter with conservative approximate diffuse attenuation, not exact glTF/multiple-scattering compensation. Depth truncation is explicit.

```text
build/Release/basix_reference_render --width 256 --height 256 --spp 64 --batch 7 --seed 20261028 --output reference-output
python tools/run_reference_validation.py --executable build/Release/basix_reference_render --output reference-validation
```

Single-configuration builds omit `Release/`; Windows executable suffix is optional. Outputs are linear RGB float32 PFM (little endian, bottom-up), a Reinhard+sRGB PPM preview and metadata. The CLI bounds image/sample/depth work and rejects invalid inputs. The validation runner tests eight rejected inputs, batch byte identity, HDR finite/nonnegative output, metadata and the actual preview transform. See `docs/reference-results.json`. Image timing is local CPU oracle cost, not a performance benchmark.

The optional GPU build adds `basix_material_gpu`. Run a Debug build with `--validate --output material-output` and process-local Khronos/core/SyncVal configuration as above. It compares2048 exact uploaded float inputs with CPU double BRDF/PDF evaluation, using abs0.0003+rel0.001Ã—|reference|; the isolated no-op control fails8192 poisoned outputs. `docs/material-results.json` records scope and results. Later GPU sampling and progressive scene results are scoped below; import, textures and motion guides remain pending.

## Vulkan capability queries and cross-API concepts

`basix_vulkan_caps --validate --output vulkan-capabilities.json` enumerates physical-device NV/NVX extensions and queries feature/property structures for16 curated candidates. It creates no logical device, enables no optional feature, and runs no advanced shader/AS operation. Matrix/vector supported type/dimension tuple enumeration remains pending. The sanitized local receipt is `docs/vulkan-capabilities.json`. The callback separately counts known loader environment warnings about installed Galaxy layer naming and layers explicitly disabled by the process filter; all other warning/error diagnostics fail. This is not a claim of zero total loader messages.

Keep Vulkan first: DX LinAlg corresponds to the shader matrix/vector inference family; clustered geometry corresponds to CLASâ†’cluster BLASâ†’TLAS. Their native layouts, limits, shader semantics, synchronization and device-enabling contracts require separate adapters. Query evidence does not complete CLAS, SER, partitioned-AS, matrix/vector, DGC, micromap or decompression implementation. Prefer suitable KHR/EXT routes while preserving ordinary compute/ray fallbacks; the docs compare current contracts and candidate gates.

## Progressive Vulkan PT and retained sampling limitation (M2-gpu partial)

Preparation patch6 adds an explicitly requested, queried/checked `basix_require_shader_int64` device requirement for the PT counter RNG. First validation rejected Int64 SPIR-V without that enable. Re-prepare a new NoGraphicsAPI tree and reconfigure; old patch5 prepared copies are rejected. Non-PT targets leave the new requirement false. CPU/default builds remain offline with no GPU dependencies.

The GPU build now includes `basix_progressive_gpu` and `basix_material_sampling_gpu`. `shaders/basix_material.slang` is the shared float material contract. The native ray adapter uses a conventional AS descriptor and8-byte push-constant BDA root. Device, command pool/timeline and mapped scene/output heap belong to NoGraphicsAPI; no upstream public RT API is implied. ABI assertions and reflected layouts cover Root176/Triangle64/Material48.

```text
build-gpu/Debug/basix_progressive_gpu --validate --width 32 --height 32 --spp 1024 --output progressive-output
python tools/run_progressive_validation.py --exe build-gpu/Debug/basix_progressive_gpu --shader build-gpu/shaders/progressive_pt.spv --validation-layer-dir PATH_TO_PINNED_VVL --output progressive-validation
build-gpu/Debug/basix_material_sampling_gpu --validate --require-paired-transport --output sampling-strict
```

Set process-local validation variables as documented above. Single-configuration builds omit `Debug/`. `--require-paired-transport` currently **exits1**: the original same-variates CPU/GPU sample criterion still fails18 of4096 extreme grazing cases. Maximum paired importance-weight difference is15.688%. It remains a failing acceptance gate. Near-cancelling `wo+wi` half-vector reconstruction amplifies float direction rounding; no shader/model/tolerance was changed to hide it. Normal sampling mode can exit0 only for separately named direction/validity and returned-tuple diagnostics, and its receipt/stdout explicitly say **partial**. Independent distribution/energy acceptance for that legacy proposal remains open; the distinct visible-GGX acceptance below is separate.

The scene harness passes seven variants and four expected rejections. It checks actual requested settings, exact executable/shader hashes, primary hits (shared-edge ties require actual edge proximity), finite/nonnegative decoded HDR, batch1/batch8 identity and reset. CPU image comparisons use the optional24-bit stream; CPU53 default golden images remain unchanged. GPU shader/no-dispatch output poisoning and invalid epoch tests reject stale/untouched results. Root/pipeline/AS/heap lifetimes retire after completed timeline submissions; full-process logs include initialization and teardown.

The fixed original box scene uses unequal-area emitter triangles, one identity TLAS instance and unit-scale float offsets. Thin gaps, large coordinates, transformed instances, import/textures, motion/DLSS integration and cross-device quality remain unverified. `--no-cpu-comparison` permits a bounded larger preview; its CPU error metrics are null, and its scope is distinct. Dispatch/readback host elapsed is not isolated GPU time or a comparative performance claim. See `docs/progressive-results.json`, `docs/sampling-results.json` and the visible roadmap logbook.


The canonical sampling harness reproduces all three outcomes with supplied local tools and no downloads:

```text
python tools/run_sampling_validation.py --exe build-gpu/Debug/basix_material_sampling_gpu --shader build-gpu/shaders/material_sampling.spv --slangc PATH_TO_PINNED_SLANGC --validation-layer-dir PATH_TO_PINNED_VVL --output sampling-validation
```

It records exact hashes, normal partial status, strict rejection and an isolated compiled no-op control. Known loader notices caused by the process's explicit implicit-layer disable filter are narrowly counted separately; all other warning/error/backend diagnostics fail. Raw logs and scratch shaders remain in the chosen work directory. The selected public receipt excludes local paths. Actual static DLSS SR was rechecked after patch6: all8cases still pass, including3real SR frames with the original image oracle. This does not add motion or temporal DLSS acceptance.

## Visible-GGX sampled-state GPU default (M2-gpu-vndf)

The GPU progressive renderer now defaults to `vndf_sampled_state_v1`. `--sampler ndf` retains the legacy control; CPU reference default remains NDF/53-bit and `--sampler vndf` selects the new proposal explicitly. The original MIT implementation follows visible-normal mathematics from Heitz2018, with generated half-vector state retained for sampled BRDF/PDF evaluation. Separable Smith/single-scatter physics and roughness limits are unchanged. Factoring and rationalizing the projected-disk height avoids disk-rim cancellation without cutoffs or altered variates.

The independent GPU oracle links neither CPU material implementation nor its sampling routines. It integrates projected-NDF half-slope radial domains with analytic azimuth arcs; physical energy uses a D-cancelled half-domain integrand. Analytic Lambert and roughness1 white-conductor anchors check the numerical oracle. Nine configurations cover normal/grazing/oblique white conductors, Lambert and one white dielectric mixture. Each uses262144 samples,65direction/null bins and three energy channels. Bonferroni Bernstein bounds assume IID pseudorandom variates; quadrature refinement envelopes are convergence estimates, not rigorous interval bounds. This is bounded material evidence, not a universal proof.

```text
python tools/run_visible_validation.py --exe build-gpu/Debug/basix_material_visible_gpu --distribution-exe build-gpu/Debug/basix_material_distribution_gpu --shader-dir build-gpu/shaders --slangc PATH_TO_PINNED_SLANGC --nographics-include PATH_TO_PREPARED_NOGRAPHICSAPI/include --validation-layer-dir PATH_TO_PINNED_VVL --output visible-validation
python tools/run_progressive_validation.py --exe build-gpu/Debug/basix_progressive_gpu --shader build-gpu/shaders/progressive_pt.spv --sampler vndf --verify-default --validation-layer-dir PATH_TO_PINNED_VVL --output visible-progressive-validation
```

The material harness passes16384 paired cases across four frozen deterministic input streams at the existing f/PDF and direction tolerances, with an explicit importance-weight tolerance. Two actual no-op shaders reject poisoned output. An isolated shader multiplying valid PDFs by1.1 fails all27 energy channels while preserving every histogram count. The harness recomputes confidence/acceptance arithmetic from serialized aggregates and scans whole-process core/SyncVal logs; hashes pin all inputs before/after. Selected numeric receipts contain no local paths.

The11-case scene suite checks the actual executable default plus lighting variants, CPU24 images, independently decoded HDR, primary intersections, accumulation/reset identity and expected failures. New CPU material tests add67 independent checks; the offline oracle also tests exact anchors and synthetic biased populations. CPU reference now has79 checks, including preserved default and visible-proposal integration semantics.

The old NDF oracle is unchanged and still fails18 cases. Reconstructing a normal from rounded nearly cancelling directions can amplify FP32 storage error; the new sampled-state contract does not make standalone rounded-direction reconstruction identical. Its separate diagnostic is preserved. M2-gpu remains partial for broader geometry/temporal acceptance. No cross-device, UE-detail, advanced extension or performance claim follows.
