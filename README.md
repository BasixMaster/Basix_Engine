# Basix Engine

An MIT engine foundation built around NoGraphicsAPI, with a research roadmap for dense geometry, many-light rendering, hardware ray tracing, full PT and optional DLSS integration.

**Status:** initial experimental CPU/GPU code is implemented and locally tested. This is not yet a complete renderer or game engine. See [implementation status](ENGINE_STATUS.md) and [build instructions](IMPLEMENTATION.md).

## Build the offline CPU foundation

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

No external dependency is required for the default C++20 cage-math target. The optional GPU experiment requires explicitly prepared NoGraphicsAPI/Vulkan/Slang dependencies; see IMPLEMENTATION.md and [the lock file](dependencies.lock.json).

Local checks: **78 CPU cage checks**, **4,096 GPU pointer-root pixels**, and **8,192 hardware-ray results** passed on the stated MSVC/RTX4090 configuration. These are correctness checks, not performance or visual-parity benchmarks. GitHub CPU/docs CI passed on Linux and Windows for the implementation commit; GPU checks remain local and other GPU vendors are unverified.

## Current roadmap status

[roadmap.json](roadmap.json) is the status source of truth. The docs show completed, in progress, partial, pending, blocked and deferred scopes with evidence, prerequisites and next tests. [ENGINE_STATUS.md](ENGINE_STATUS.md) is generated from that ledger. Scoped validation/native texture and repeated-device/in-flight lifecycle gates pass. Parent M1 remains partial. Hidden-window resizing/restoration passes; static actual SR evaluation now passes; temporal renderer quality, OS minimize and device-loss recovery remain open. The research update adds isolated ReSTIR/LOD, procedural and neural adoption gates.

See [M1 validation results](docs/validation-results.json): five cases, three positive cycles, 25,500 texture checks and 63 contract rejections with zero warning/error logs under the named Debug/core/SyncVal configuration.

## Research documentation

Open [docs/index.html](docs/index.html) after cloning/downloading. It works offline with chapter/source search and Light/Dark/System themes.

The 18 chapters and 69 sources cover UE5.8/6, MegaLights, PETAFAB, NoGraphicsAPI, DX12/DXR, Linear Algebra, DLSS, RTXDI/RTXPT, DXR2/Clustered Geometry/PTLAS, tetrahedral cages, DGF/DGFS and other viable ideas. The revised roadmap separates tested code, proposed experiments, preview/draft features and vendor claims.

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
