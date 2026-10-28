# Basix project guidance

Read README.md and docs/index.html before changing architecture. Research date and dependency revisions are explicit; refresh mutable claims from primary sources before updating them. Distinguish proposals, vendor claims and actual measurements.

Keep original engine code MIT. Optional SDKs retain separate terms; do not vendor NVIDIA or UE implementation code under MIT. Default builds must work without proprietary SDK downloads once implementation exists.

Edit tools/build_docs.py and tools/research_expansion.py to change research content; regenerate with Python and run tools/check_docs.py. Validate desktop/mobile appearance, both themes and search after layout changes. Keep the private local meter out of shared/public exports. Do not ship the personal-use author PDF, extracted text, private feeds or screenshots containing private account telemetry.

Engine sources use C++20. Default CMake builds must stay offline and dependency-free; GPU experiments require explicit prepared, pinned dependencies. Run CMake/CTest for affected code. Keep experiment receipts and GPU/driver configuration separate from performance claims. The NoGraphicsAPI patch preparation must preserve its clean upstream source and retain notices in the separate generated tree. Do not claim full PT, DLSS, DXR2 or cage GPU traversal from the existing triangle/mathematics tests.

Maintain roadmap.json as the milestone source of truth and preserve its dated status logbook. Record scoped evidence, previous/new state and next action for meaningful transitions; regenerate ENGINE_STATUS.md and the HTML projection, then run roadmap/docs checks. Completed static SR interoperability does not complete temporal DLSS quality, RR/FG or the parent recovery gate. Keep SDK raw logs/local paths private; publish only selected sanitized receipts.
