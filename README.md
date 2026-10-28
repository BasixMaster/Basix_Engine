# Basix Engine

Research and architecture for an MIT game engine built around NoGraphicsAPI, with rich geometry, many-light rendering, hardware ray tracing, path tracing and optional DLSS integration.

**Status:** documentation and design proposals only. No engine implementation or rendering benchmark is included yet.

## Read the research

Open [docs/index.html](docs/index.html) in a browser after cloning or downloading this repository. The self-contained documentation works offline, including chapter/source search and Light/Dark/System themes.

The page covers:

- UE5.8 and UE6 status, and concrete requirements behind “UE-like detail”.
- NoGraphicsAPI source/API assessment and proposed hardware RT/native SDK extensions.
- PETAFAB’s publicly described SDF/mesh-shader technology and limits of the evidence.
- MegaLights versus RTXDI, shared materials, hybrid rendering and full PT.
- DXR1.2, preview DirectX Linear Algebra and draft DXR2 availability.
- DLSS resource/guide contracts, MIT distribution boundaries, milestones and acceptance tests.

Research date: **28 October 2026**. Vendor claims, source inspection and proposed designs are explicitly distinguished. Roadmaps can change.

## Rebuild and verify

Python 3.10+; standard library only:

```text
python tools/build_docs.py
python tools/check_docs.py
```

For a registered private local chat meter, use `python tools/build_docs.py --private-meter`. It uses the owning workspace adapter and this page’s binding. Default builds stay meter-free; strip private blocks through the owning sanitized export before publication. The SVG identity and banner are original Basix artwork.

The editable research source is `tools/build_docs.py`. Structured evidence lives in [docs/evidence/library.json](docs/evidence/library.json), with a SQLite FTS index alongside it. Exact inspected dependency revisions are in [docs/dependencies.json](docs/dependencies.json). Documentation verification results are in [docs/verification.json](docs/verification.json).

Browser checks also passed for offline search, anchors, mobile overflow, saved/system themes and desktop/mobile light/dark views; see [browser verification](docs/browser-verification.json). To repeat, provide Node.js and Playwright, install its Chromium browser, then run `node tools/check_browser.cjs`. Alternatively set `PLAYWRIGHT_CHANNEL=msedge` to use an installed Edge. Screenshots and the fresh browser receipt are written under ignored `work/`.

To serve locally, run `python -m http.server 8000 --directory docs` and open `http://localhost:8000`. GitHub Pages hosting is not configured by this initial research delivery.

## License

Original Basix documentation and original future engine code use MIT; see [LICENSE](LICENSE). External source material retains its own rights. NoGraphicsAPI is MIT at the reviewed revision; NVIDIA DLSS, RTXDI, RTXPT and NRD retain their separate NVIDIA terms. An MIT core does **not** make a bundled SDK stack entirely MIT. No SDK source/binaries or game assets are vendored here.

## Next implementation gate

Prove NoGraphicsAPI hardware RT and native SDK interop with a moving triangle, resize/history reset and a working fallback before building a broad editor or virtualized geometry system.
