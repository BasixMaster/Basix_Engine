# Basix Engine

An MIT engine foundation built around NoGraphicsAPI, with a research roadmap for dense geometry, many-light rendering, hardware ray tracing, full PT and optional DLSS integration.

**Status:** initial experimental CPU/GPU code is implemented and locally tested. This is not yet a complete renderer or game engine. See [implementation status](ENGINE_STATUS.md) and [build instructions](IMPLEMENTATION.md).

## Build the offline CPU foundation

```text

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release

ctest --test-dir build -C Release --output-on-failure

```

No external dependency is required for the default C++20 cage, material and CPU reference-renderer targets. The optional GPU experiment requires explicitly prepared NoGraphicsAPI/Vulkan/Slang dependencies; see IMPLEMENTATION.md and [the lock file](dependencies.lock.json).

Local checks: **78 CPU cage checks**, **4,096 GPU pointer-root pixels**, and **8,192 hardware-ray results** passed on the stated MSVC/RTX4090 configuration. These are correctness checks, not performance or visual-parity benchmarks. GitHub CPU/docs CI passed on Linux and Windows for the implementation commit; GPU checks remain local and other GPU vendors are unverified.

## Current roadmap status

[roadmap.json](roadmap.json) is the status source of truth. The docs show completed, in progress, partial, pending, blocked and deferred scopes with evidence, prerequisites and next tests. [ENGINE_STATUS.md](ENGINE_STATUS.md) is generated from that ledger. Scoped validation/native texture and repeated-device/in-flight lifecycle gates pass. Parent M1 remains partial. Hidden-window resizing/restoration passes; static actual SR evaluation now passes; temporal renderer quality, OS minimize and device-loss recovery remain open. The research update adds isolated ReSTIR/LOD, procedural and neural adoption gates.

See [M1 validation results](docs/validation-results.json): five cases, three positive cycles, 25,500 texture checks and 63 contract rejections with zero warning/error logs under the named Debug/core/SyncVal configuration.

## Research documentation

Open [docs/index.html](docs/index.html) after cloning/downloading. It works offline with chapter/source search and Light/Dark/System themes.

The 20 chapters and 112 sources cover UE5.8/6, MegaLights, PETAFAB, NoGraphicsAPI, DX12/DXR, Linear Algebra, DLSS, RTXDI/RTXPT, DXR2/Clustered Geometry/PTLAS, tetrahedral cages, DGF/DGFS and other viable ideas. The revised roadmap separates tested code, proposed experiments, preview/draft features and vendor claims.

Research date: **28 October 2026**. DXR2’s rendered spec says preview while other official maturity/package evidence conflicts; a matching public SDK/driver combination has not been established here.

## Rebuild documentation

Python 3.10+; standard library only:

```text

python tools/build_docs.py

python tools/check_docs.py

```

Editable sources are `tools/build_docs.py` and the content modules `tools/research_expansion.py`, `tools/thread_research.py` and `tools/sdk_status.py`. Structured evidence is in [docs/evidence/library.json](docs/evidence/library.json), with a SQLite FTS index alongside it. [Dependency research pins](docs/dependencies.json), [implementation receipt](docs/implementation-results.json), and [documentation verification](docs/verification.json) retain their distinct scopes.

Browser checks cover offline search, anchors, mobile overflow and saved/system themes. Repeat with Node.js/Playwright: `node tools/check_browser.cjs`; set `PLAYWRIGHT_CHANNEL=msedge` to use installed Edge. Screenshots/receipt go into ignored `work/`.

For a registered private local meter, use `python tools/build_docs.py --private-meter`. It uses the owning workspace adapter and this page’s chat-family binding. Default builds are meter-free. Strip private blocks/feeds through the owning sanitized export before publication. The SVG identity/banner are original Basix artwork.

Serve locally with `python -m http.server 8000 --directory docs`. GitHub Pages hosting is not configured.

## License and provenance

Original Basix code/docs use [MIT](LICENSE). NoGraphicsAPI is MIT at the reviewed revision; its modified build copy retains upstream notices. NVIDIA DLSS, RTXDI, RTXPT and NRD keep separate NVIDIA terms; an MIT core does not make a bundled SDK stack entirely MIT. No proprietary SDK code/binaries, game assets, author PDF or extracted paper text are vendored.

The CPU cage code is an original mathematical experiment, not copied from AMD or the independent Apache-2.0 reference. All new candidate dependencies remain research-only until an exact adoption revision and complete notice/dependency audit is performed.

The optional actual SDK test and eight-case receipt are documented in [IMPLEMENTATION.md](IMPLEMENTATION.md) and [docs/dlss-results.json](docs/dlss-results.json). NVIDIA runtime binaries remain separately supplied. The roadmap also displays a dated status logbook; structured transitions must agree with current states and preserve evidence.

M2 is now partial:70 CPU material checks,85 CPU transport checks and2,048 GPU BRDF/PDF cases pass. The original CPU scene preview and scoped receipts are in [the reference chapter](docs/index.html#reference-renderer). A scoped Vulkan progressive scene now passes;18 extreme grazing sampling cases remain unresolved, while importer and temporal guides are pending. [Vulkan feature queries](docs/vulkan-capabilities.json) confirm candidate support only; CLAS/matrix/vector execution remains unimplemented.

The progressive scene gate uses ordinary Vulkan ray queries on the NoGraphicsAPI-owned device. [GPU results](docs/progressive-results.json) and [partial sampling results](docs/sampling-results.json) retain the acceptance limits and original failed criterion. No advanced extension execution is claimed.

Visible-GGX sampled-state transport is now the GPU default, with independent distribution/energy acceptance and actual shader fault controls. CPU rendering keeps the legacy default; select `--sampler vndf` explicitly. [Sampling results](docs/visible-results.json) and [default GPU scene checks](docs/visible-progressive-results.json) define the tested scope. The legacy18-case grazing discrepancy remains recorded separately.

An opt-in `--bounded-rays` candidate now uses triangle error boxes and two-ended shadow connections. [180 ray fixtures and fault controls](docs/ray-spawn-results.json) and [candidate scene tests](docs/ray-progressive-results.json) pass for the stated identity-instance scope. Later boundary and fixed-transform evidence is scoped separately below; the default offset policy stays unchanged.

The hardware boundary gate now passes360 fixtures, including72 exact vertex and72 exact edge hits. Actual PT rendering supports a bounded static instance catalog via `--instance-transform 0..5`; eight CPU/GPU scenes and four rejection controls pass. Reflected rendering preserves authored emitting/outward sides. [Boundary results](docs/ray-boundary-results.json) and [instance scene results](docs/instance-progressive-results.json) define the scope; bounded rays stay opt-in.

Shared-BLAS implementation now separates dense custom slots from stable instance/primitive IDs, validates metadata epochs and preserves authored sides without changing shared vertex order. Two actual in-place TLAS updates and refusal-before-write checks are implemented on one owned queue. A larger preview exposed coplanar floor overlap between different instances. Render-only placement separation fixes that fixture ambiguity while preserving strict hit checks and the probe catalog; the corrected 128×64/128-SPP preview passes three epochs with restored HDR byte identity. The expanded ten-run harness passes: 48 positive probes, six rendered epochs and eight rejection controls, including independent checks of actual room matrices/bounds at both image sizes. The bounded shared/static and synchronous-update milestones are completed; their general parent remains partial. CPU instance/reference suites pass 68/85 checks respectively. General transform-conditioned uncertainty, asynchronous/multiqueue lifetimes and importer/temporal guides remain open. See [implementation details](IMPLEMENTATION.md#shared-blas-identity-and-synchronized-updates) and [the roadmap](ENGINE_STATUS.md).

The bounded transform uncertainty/range gate now passes 78 CPU checks, 96 actual GPU fixtures and six rejection controls. Its opt-in renderer passes unchanged image limits, with a larger paired error than the earlier control; control HDR remains byte-identical. Arithmetic enclosure and conditional hardware policies remain distinct. Authored-scene depth/motion/material guides pass the seven-run 20,304-pixel gate and 110 CPU checks. Actual same-device renderer-to-SR lifecycle passes an eight-case Debug/Release gate, including 10 SDK evaluations/one fallback, resize/reset/toggles, history recovery and SDK-absent 11-frame bilinear fallback. These bounded subgates are completed; independent temporal/detail quality remains pending. SR is not a PT denoiser and RR/FG remain separate. M3 implementation is currently focused on DLSS; starting other M3 work, including RTXDI/many-light integration, requires advance notice to the user.
