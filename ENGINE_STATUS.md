# Engine roadmap status — 2026-10-28

Generated from `roadmap.json`. States describe only the named scope; no overall completion percentage is implied.

| Gate | Status | Verified scope / remaining work |
| --- | --- | --- |
| M0a · CPU foundation | completed | Dependency-free C++20 build and original affine tetrahedral-cell math; not cage preprocessing or GPU cage traversal. |
| M0b · GPU pointer-root foundation | completed | Pinned NoGraphicsAPI preparation and one Slang pointer-root ABI on the named Windows/Vulkan adapter. |
| M1a · Ordinary hardware-ray seam | completed | Same-device Vulkan BLAS/TLAS and static/translated ray queries using native command scopes. |
| M1b · Harden native interop | partial | Validation, native texture/resource-state contract, SDK transitions and lifecycle behavior. |
| M1b-validation · Validation-enabled GPU correctness | completed | Named local Debug core/SyncVal run plus whole-process log scan, pinned validation package and missing-shader/image-write/Release-rejection cases. One RTX4090 configuration only. |
| M1b-textures · Native texture and layout contract | completed | Controlled same-device/same-queue RGBA8 2D images: GENERAL/transfer layout transitions, native compute and explicit NoGraphicsAPI heap/PSO restore; registry-issued tickets and fence-gated retirement. |
| M1b-sdk · External SDK state restoration | completed | Actual optional signed Streamline2.14.16 SR evaluation on NoGraphicsAPI-owned Vulkan device; static constant radiance and same-command native/engine binding restoration. |
| M1b-lifecycle · Repeated resource lifecycle | completed | Three actual device sessions, three resource cycles each, held asynchronous submissions, real incomplete-timeline retirement rejection and registry-incarnation isolation on the named local adapter. |
| M1b-resize · Window resize and device loss | partial | Hidden Win32 swapchain resize, zero client extent and restoration pass. Actual OS minimize, device-loss recovery and visible presentation quality remain unverified. |
| M2 · Reference renderer and materials | partial | Stable scene/material IDs, import, shared BSDF and progressive reference PT. |
| M3 · Many lights and reconstruction | pending | Original direct lighting/fallback denoising and optional RTXDI/SR/RR. A separate static SR interoperability spike is tested; integrated renderer lighting/temporal quality remains pending. |
| M3-dlss · DLSS integration | partial | Actual static SR/interoperability smoke passes. Shared renderer guides, temporal quality, camera cuts/resize/toggles and RR remain unimplemented or unverified; FG separately scoped. |
| G1 · Geometry and streaming contract | pending | Stable clusters, codec tags, residency generations, source triangle/material provenance and conventional BLAS; explicit surface correspondence for temporal LOD changes. |
| G2 · Tetrahedral cage pipeline | partial | Original affine-cell CPU math is verified; cage generation, clipping, attributes and GPU adjacent-cell traversal remain open. |
| G2-gpu · GPU cages and shared boundaries | pending | Adjacent-cell micro-BLAS tracing, motion history and deformation correctness; not implemented by ordinary triangle ray smoke. |
| G3 · Optional clustered acceleration structures | pending | NV cluster experiment and later separate standard DXR2 adapter; advertised device extensions are not implementation evidence. |
| G4 · Geometry comparison | pending | Compare conventional skin/refit, rigid instances, clusters and cages on repeated and diverse assets. |
| M4 · Playable game slice | pending | Streaming scene, animation, physics/audio/input, save/load and packaged assets. |
| R · Optional rendering research | deferred | DGF/DGFS, LinAlg, neural caches, Work Graphs and future Work Lists are independently optional. |
| R-temporal · Advanced temporal sampling | deferred | LOD-aware reuse, multilayer splatting, PT Enhanced and variable rate tracing; optional independent experiments. |
| R-guiding · Optional path guiding | deferred | ReSTIR PG candidate generation with BSDF fallback and explicit path/grid memory budget. |
| R-neural · Optional neural texture and residual cache | deferred | Independent texture inference and cache-plus-residual branches; MiniDXNN MIT reference, GPLv3 NIRC source excluded from MIT core. Vulkan-first matrix/vector adapters; DirectX LinAlg is a corresponding technique family, not a shared API. |
| R-procedural · Procedural vegetation research | deferred | Deterministic compact vegetation grammar with compute/mesh fallback; Work Graphs mesh nodes optional D3D12 experiment. |
| M2-material · Shared material oracle and GPU evaluation | completed | Original MIT Lambert and single-scatter isotropic GGX model; CPU sampling/PDF and paired GPU evaluation/PDF only. |
| M2-reference · CPU progressive reference PT | completed | Stable scene/material IDs, analytic spheres/triangles, one-sided triangle emitters, NEE/power MIS/compensated RR and deterministic accumulation. Optional24-bit exact uniform stream now supports matched GPU comparisons; default53-bit output is preserved. |
| M2-gpu · Vulkan progressive reference PT | partial | Pointer-root triangle scene/material layout, ordinary Vulkan BLAS/TLAS ray-query progressive PT and shared GPU sampling; fixed-scene transport passes while18 extreme grazing same-variates sample cases remain outside the original criterion. |
| M2-guides · Scene import and temporal guides | pending | Minimal importer with stable IDs, texture/material metadata, linear depth, camera/object motion and reset clips. |
| V1-query · Vulkan extension capability inventory | completed | Physical-device feature/property queries for16 curated candidates plus NV/NVX name inventory; no logical device or advanced extension execution. |
| M2-gpu-scene · Fixed-scene Vulkan progressive PT | completed | Original unit-scale triangle fixture, one identity instance; NEE/MIS/RR, ordinary AS, primary rays, accumulation and reset only. |
| M2-gpu-numerics · Grazing sampling robustness and distributions | partial | Direction/validity and returned-tuple contracts measured; original same-variates sample criterion fails18 of4096 extreme cases, maximum paired weight discrepancy15.688%. |

## Milestone and status logbook

Entries retain their recorded order. Legacy notes remain visible without asserting unrecorded status transitions.

| Date | Item | Status change | Event / evidence |
| --- | --- | --- | --- |
| 2026-10-28 | General / legacy note | No structured transition recorded | Recorded baseline completed M0a/M0b/M1a and pending future gates. |
| 2026-10-28 | General / legacy note | No structured transition recorded | Completed scoped M1b-validation and M1b-textures from the five-case local validation harness; parent/lifecycle remain partial. |
| 2026-10-28 | General / legacy note | No structured transition recorded | Evaluated public forum leads against primary sources; added deferred sampling/guiding/neural/procedural gates and LOD provenance acceptance. No implementation status promoted by research. |
| 2026-10-28 | General / legacy note | No structured transition recorded | Completed scoped repeated-device/in-flight lifecycle gate; hidden-window resize/restoration passed but resize/recovery parent remains partial. SDK and actual device loss stay open. |
| 2026-10-28 | M1b-sdk | pending → completed | Completed bounded actual SR/resource-state interoperability with measured issue109 workaround; static fixture only.<br>Evidence: [docs/dlss-results.json](docs/dlss-results.json) |
| 2026-10-28 | M3-dlss | pending → partial | Static SR seam passes; shared renderer temporal guides, resets/fallbacks and RR remain open.<br>Evidence: [docs/dlss-results.json](docs/dlss-results.json) |
| 2026-10-28 | M2 | pending → partial | Completed scoped material oracle/GPU evaluation and CPU progressive reference. GPU PT, importer and motion guides remain pending; recovery gates unchanged.<br>Evidence: [docs/material-results.json](docs/material-results.json)<br>Evidence: [docs/reference-results.json](docs/reference-results.json) |
| 2026-10-28 | General / legacy note | No structured transition recorded | Recorded queried Vulkan capabilities and technique counterparts. V1-query completed only for queries; G3 and neural extension execution remain pending/deferred.<br>Evidence: [docs/vulkan-capabilities.json](docs/vulkan-capabilities.json) |
| 2026-10-28 | M2-gpu | pending → in progress | Implementing shared GPU BSDF sampling and ordinary Vulkan ray-query progressive renderer; scoped acceptance underway, grazing-conditioning failures retained. |
| 2026-10-28 | M2-gpu | in progress → partial | Scoped Vulkan progressive scene passes; retained18-case extreme-grazing sample failure prevents completing the broader GPU transport gate.<br>Evidence: [docs/progressive-results.json](docs/progressive-results.json)<br>Evidence: [docs/sampling-results.json](docs/sampling-results.json) |
| 2026-10-28 | General / legacy note | No structured transition recorded | Recorded separate completed M2-gpu-scene and partial M2-gpu-numerics subgates. Checked optional shaderInt64 requirement after first validation rejection; original model/tolerances and failed control preserved.<br>Evidence: [docs/progressive-results.json](docs/progressive-results.json)<br>Evidence: [docs/sampling-results.json](docs/sampling-results.json) |

Build and test instructions: [IMPLEMENTATION.md](IMPLEMENTATION.md). Evidence and next tests are recorded per gate in [roadmap.json](roadmap.json) and shown in the documentation page.
