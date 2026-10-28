# Basix project guidance

Read README.md and docs/index.html before changing architecture. Research date and dependency revisions are explicit; refresh mutable claims from primary sources before updating them. Distinguish proposals, vendor claims and actual measurements.

Keep original engine code MIT. Optional SDKs retain separate terms; do not vendor NVIDIA or UE implementation code under MIT. Default builds must work without proprietary SDK downloads once implementation exists.

Edit tools/build_docs.py to change research content; regenerate with Python and run tools/check_docs.py. Validate desktop/mobile appearance, both themes and search after layout changes. Do not claim runtime feasibility from documentation checks.
