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

The test owns its device through NoGraphicsAPI. It dispatches a NoGraphicsAPI pointer-root compute shader, then builds ordinary Vulkan BLAS/TLAS objects and executes ray queries on that **same device**. Native Vulkan code is isolated in the experiment; native pipelines use a separate command-recording scope. Submissions are completed before pool reset or resource destruction; the lifecycle test deliberately holds a real submission behind a host-signaled timeline gate. Static and translated triangle results are compared with an independent analytic CPU oracle over two 64×64 grids.

Run `basix_gpu_spike --output DIRECTORY` for PPM images and a JSON result. The directory is explicit and receives only generated experiment files. Unsupported initialization fails rather than silently substituting software results. No DLSS binary, ray pipeline, DXR2/CLAS implementation or advanced rendering is included. The texture experiment uses registry-issued monotonic borrow tickets, expected metadata checks and timeline-gated retirement. Only own RGBA8 2D single-mip images on the same device/queue are covered.

## Scoped M1 validation and texture checks

Prepare a new dependency destination with the current tool. GPU CMake now requires interop patch version3; stale prepared copies fail explicitly. Compile Debug and Release, optionally configuring `-DBASIX_VALIDATION_TESTS=ON` for the Debug-only CTest case.

Use a separately installed or locally extracted Khronos validation package. The checked Windows nightly archive and DLL digests are in `dependencies.lock.json`; the mutable nightly URL does not promise future availability of those exact bytes. The layers are testing tools, not vendored/shipped engine runtime. No machine-wide loader setting is changed.

```text
python tools/run_gpu_validation.py --exe build-gpu/Debug/basix_gpu_spike.exe --release-exe build-gpu/Release/basix_gpu_spike.exe --validation-layer-dir PATH_TO_CHECKED_LAYERS --output work/validation
```

The helper sets `VK_LAYER_PATH`, disables implicit third-party overlays only for its child process, and requests core plus synchronization validation with `VK_VALIDATION_VALIDATE_SYNC=1`. It verifies the pinned DLL hash and loader trace, scans full-process logs, and checks three positive cycles plus four expected-failure cases. Full logs remain in the selected output directory; the sanitized receipt contains no local paths/account telemetry.

The GPU experiment writes a NoGraphicsAPI storage texture, transitions GENERAL→TRANSFER_SRC for a native copy, returns to GENERAL, runs a native storage-image compute pass, then explicitly restores NoGraphicsAPI heap/PSO bindings before engine readback. Tests recreate64×64,17×9 and1×1 offscreen images. Borrow tickets capture their epoch privately; epochs do not reset between cycles. Wrong metadata, stale tickets and premature retirement are rejected before native API use.

`--validate` refuses an unvalidated Release build. `--cycles 1..16` bounds resource churn. `--shader-dir` selects a shader resource directory and supports failure-path testing. Standalone C++ diagnostic counters cover the messenger lifetime after device initialization and before device teardown; the helper separately scans startup and shutdown logs. Zero validation messages are evidence for the named workload, not a proof covering every GPU hazard.

## Current verified scope

- CPU:78 independent cage-math checks passed; no GPU cage/preprocessing claim.
- Initial RTX4090/NVIDIA617.42:4096 pointer-root pixels and8192 hardware rays passed.
- Reviewed M1 Debug/core/SyncVal harness:5cases;3positive cycles;12288 pointer pixels,24576 rays,25500 exact texture pixel checks,63 metadata/stale-ticket/retirement rejections. Zero warning/error logs including startup/shutdown. Layer manifest reports1.4.366; checked archive/DLL hashes are pinned.
- Expected failures: invalid cycle count, missing-shader cleanup, image-write-failure cleanup, and Release refusal to claim validation.
- CPU/docs CI passed on Linux/Windows for prior foundation commit923d10b in run40677723983. Current changes receive their own CI after publication; GPU tests remain local physical-device checks.

## Remaining gates

The authoritative states, evidence and next tests are in `roadmap.json`; `ENGINE_STATUS.md` and the documentation roadmap table are generated from it. M1 validation and the controlled native-texture subgates are complete. The repeated-device/in-flight lifecycle gate is now complete for the named local configuration. Parent M1 remains partial. Actual optional SDK evaluation/resource tagging and device-loss recovery are pending; hidden-window resizing/zero-size restoration pass but actual OS minimize remains untested.

Full PT, DLSS, geometry streaming, cage generation/clipping/GPU traversal and clustered-AS backends are still pending. No independent spirv-val, cross-vendor GPU validation or performance benchmark is claimed. The fixture's size changes are not window resizing. Public CI runs offline CPU/roadmap/docs checks and needs no GPU SDK downloads.

## Repeated device sessions and real pending retirement

Interop patch3 adds a narrow host timeline-signal helper. Each texture submission waits for an initially unsignaled gate. The fixture marks the borrow in flight, queries the actual completion timeline, verifies it is below the submitted serial, and rejects retirement with that real value. It releases the gate, waits, checks the pixels, then retires. A scoped gate drains work before dependent resources are destroyed, including the injected exception path. No pool reset occurs while its commands are pending.

Private registry-incarnation cookies complement per-image epochs. Two fresh registries with identical device/image/extent/epoch metadata reject each other's tickets, so recycled native handles cannot impersonate another registry session. Device sessions are destroyed before the success receipt is written; the wrapper scans complete startup/shutdown logs.

```text
python tools/run_gpu_validation.py --exe build-gpu/Debug/basix_gpu_spike.exe --release-exe build-gpu/Release/basix_gpu_spike.exe --validation-layer-dir PATH_TO_CHECKED_LAYERS --sessions 3 --output work/lifecycle
python tools/run_window_validation.py --executable build-gpu/Debug/basix_window_smoke.exe --layer-dir PATH_TO_CHECKED_LAYERS --output work/window
```

The seven-case lifecycle harness passed36864 pointer pixels,73728 ray checks,76500 texture comparisons,189 contract rejections and27 registry-incarnation checks across3devices×3cycles. Of the contract rejections,27 use truly incomplete GPU submissions. This is bounded correctness, not concurrent frames, multiqueue support or a performance benchmark. See `docs/lifecycle-results.json`.

## Hidden Win32 swapchain test

The WIN32-only console target creates a never-visible, offscreen popup. It presents8clear frames across64×64,320×180,17×9 extents and a96×54 restoration; three acquisitions at a real0×0 client extent return empty. The four-process wrapper also tests controlled exceptions before device creation, after creation and after a completed present. All windows are destroyed and full-process validation logs are clean. Receipts are in `docs/window-results.json`.

The first run exposed an upstream swapchain format/usage issue: sRGB plus STORAGE usage was unsupported on the tested adapter (VUID01778/02275). The clear-only test uses supported `bgra8_unorm`; validation was retained. Original failure logs stay in the workbench. This does not prove sRGB presentation support, displayed pixels, OS minimize, device loss or SDK correctness.
