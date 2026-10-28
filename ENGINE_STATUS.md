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
| M1b-sdk · External SDK state restoration | pending | Resource tagging and state restoration around an actual optional reconstruction SDK evaluation. |
| M1b-lifecycle · Repeated resource lifecycle | partial | Nine offscreen image lifecycles across three cycles and negative retirement guards are tested. Repeated device creation and real asynchronous/in-flight release scenarios remain unverified. |
| M1b-resize · Window resize and device loss | pending | Window/swapchain resize and explicit device-loss/recovery behavior; current headless test does not cover this. |
| M2 · Reference renderer and materials | pending | Stable scene/material IDs, import, shared BSDF and progressive reference PT. |
| M3 · Many lights and reconstruction | pending | Original direct lighting/fallback denoising, optional RTXDI, SR then RR; no vendor SDK is currently integrated. |
| M3-dlss · DLSS integration | pending | Optional SR followed by RR through explicit native resource/guide adapters; frame generation separately scoped. |
| G1 · Geometry and streaming contract | pending | Stable clusters, codec tags, residency generations, source triangle/material provenance and conventional BLAS. |
| G2 · Tetrahedral cage pipeline | partial | Original affine-cell CPU math is verified; cage generation, clipping, attributes and GPU adjacent-cell traversal remain open. |
| G2-gpu · GPU cages and shared boundaries | pending | Adjacent-cell micro-BLAS tracing, motion history and deformation correctness; not implemented by ordinary triangle ray smoke. |
| G3 · Optional clustered acceleration structures | pending | NV cluster experiment and later separate standard DXR2 adapter; advertised device extensions are not implementation evidence. |
| G4 · Geometry comparison | pending | Compare conventional skin/refit, rigid instances, clusters and cages on repeated and diverse assets. |
| M4 · Playable game slice | pending | Streaming scene, animation, physics/audio/input, save/load and packaged assets. |
| R · Optional rendering research | deferred | DGF/DGFS, LinAlg, neural caches, Work Graphs and future Work Lists are independently optional. |

Build and test instructions: [IMPLEMENTATION.md](IMPLEMENTATION.md). Evidence and next tests are recorded per gate in [roadmap.json](roadmap.json) and shown in the documentation page.
