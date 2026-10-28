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

The preparation tool verifies the exact clean upstream revision and refuses to overwrite an existing destination. It copies the upstream tree and applies an original experimental Vulkan-only extension: explicit ray-query device initialization and borrowed device/physical-device/command-buffer handles. The upstream checkout stays unchanged. This extension is not an upstream-supported API or a Metal backend implementation.

Only committed bytes are exported (`git archive`), so ignored local files cannot enter the prepared dependency. GPU CMake validates the exact revision marker and tracks the included shader ABI/compiler as build dependencies. Apple is explicitly excluded from this Vulkan-only spike; the CPU target remains portable. RT eligibility is currently checked after upstream adapter selection, so a multi-GPU machine may reject the selected adapter even if another could run RT; explicit adapter selection remains open.

The test owns its device through NoGraphicsAPI. It dispatches a NoGraphicsAPI pointer-root compute shader, then builds ordinary Vulkan BLAS/TLAS objects and executes ray queries on that **same device**. Native Vulkan code is isolated in the experiment; native pipelines use a separate command-recording scope. Every submission is awaited before pool reset or resource destruction. Static and translated triangle results are compared with an independent analytic CPU oracle over two 64×64 grids.

Run `basix_gpu_spike --output DIRECTORY` for PPM images and a JSON result. The directory is explicit and receives only generated experiment files. Unsupported initialization fails rather than silently substituting software results. No DLSS binary, ray pipeline, DXR2/CLAS implementation or advanced rendering is included.

## Verified scope and remaining gates

- Local CPU: 78 checks passed.
- Local RTX 4090 / NVIDIA 617.42: pointer-root 4,096-pixel oracle and 8,192 hardware-ray results passed.
- GPU test is headless and correctness-only. No Khronos validation-layer pass or performance benchmark is claimed. SPIR-V was produced by the pinned Slang compiler; an independent spirv-val check was not available.
- Local GPU runs disabled implicit third-party overlay layers for that process with `VK_LOADER_LAYERS_DISABLE=~implicit~`; no system registry or driver settings were changed.
- GPU window resize, presentation, device-loss recovery, textures/native SDK state contracts, DLSS SR/RR and mixed-renderer state restoration remain open.
- Cage clipping of source triangles, shared-vertex provenance, cage generation, serialized assets, multi-tet GPU traversal and deformation motion vectors remain open.
- Clustered geometry, PTLAS, DGF/DGFS, meshlet streaming and Work Lists are idea-pool items, with distinct availability and adoption gates in the research page.

CI runs the offline CPU and public-documentation targets; GPU CI requires a separately configured physical runner. CPU/docs CI for implementation commit923d10b passed on Linux and Windows in [run40677723983](https://github.com/BasixMaster/Basix_Engine/actions/runs/40677723983). GPU success remains local, with no cross-vendor validation.
