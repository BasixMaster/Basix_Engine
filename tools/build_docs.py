"""Build the portable Basix research page and its searchable evidence records. Python stdlib only."""
from pathlib import Path
import html, json, re, sqlite3

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / 'docs'
DATE = '2026-10-28'
NG = 'ffd9f2ab6b7442dcfbe0cb0051d84d0edb078d89'
PINS = {
 'NoGraphicsAPI': ('sebbbi/NoGraphicsAPI', NG, 'LICENSE', 'MIT'),
 'DLSS': ('NVIDIA/DLSS','9981c6350dc58c0ce21f024fd65e31e851c31689','LICENSE.txt','NVIDIA RTX SDKs License'),
 'RTXDI': ('NVIDIA-RTX/RTXDI','a6efab966b7c3b272da0461578eb56ac61c7cbff','LICENSE.txt','NVIDIA RTX SDKs License'),
 'RTXDI-Library': ('NVIDIA-RTX/RTXDI-Library','f12037fa8e97ebc08e9e3edfd2de528ed1772a4b','LICENSE.txt','NVIDIA RTX SDKs License'),
 'RTXPT': ('NVIDIA-RTX/RTXPT','303b408ab81790a5051abd7573830b7a4112eb8e','LICENSE.txt','NVIDIA RTX SDKs License'),
 'NRD': ('NVIDIA-RTX/NRD','9afa555ead6936be1d8f7f2713ac770e2821a83a','LICENSE.txt','NVIDIA RTX SDKs License'),
 'Streamline': ('NVIDIA-RTX/Streamline','625db07d4bca5abc67d781c42af7e7fb0761f1b6','license.txt','MIT framework; Nsight Perf exceptions'),
}
sources=[]
def source(id,title,url,notes,kind='official documentation',date='',rights='Not copied; linked and summarized.'):
 sources.append(dict(id=id,title=title,url=url,notes=notes,kind=kind,published_on=date,checked_on=DATE,access='full',quality='Primary source maintained by the technology owner; relevant to the stated scope.',limitations='Mutable upstream material and vendor reports do not establish Basix compatibility or measured performance.',rights=rights))
source('S01','NoGraphicsAPI README',f'https://github.com/sebbbi/NoGraphicsAPI/blob/{NG}/README.md','C++20, Vulkan 1.4 and Metal 4 implementation; required Vulkan extensions exceed core 1.4. Driver compatibility table is explicitly a September 2026 snapshot.')
source('S02','NoGraphicsAPI public interface',f'https://github.com/sebbbi/NoGraphicsAPI/blob/{NG}/include/NoGraphicsAPI/NoGraphicsAPI.hpp','Local source inspection: no public acceleration-structure build, trace-rays pipeline, or native-handle interop API in this header. Existing draw, compute, mesh, allocation and synchronization API is not a complete hardware RT API.',kind='code inspection')
source('S03','NoGraphicsAPI design comparison',f'https://github.com/sebbbi/NoGraphicsAPI/blob/{NG}/docs/no-graphics-api-comparison.md','Pointer roots, application-owned lifetimes and indexed descriptor heaps; barriers use stage/access scopes. Implementation differs from the original aspirational blog.')
source('S04','NoGraphicsAPI Slang ABI',f'https://github.com/sebbbi/NoGraphicsAPI/blob/{NG}/docs/slang.md','16-byte-aligned root records; shared constant-buffer layout differs from pointed-to C POD data. Compile and reflect layout checks. Vulkan and Metal shader artifact requirements differ.')
source('S05','PETAFAB developer technology page','https://www.sebastianaaltonen.com/petafab/','Developer reports custom NoGraphicsAPI engine, distance-field ray tracing, mesh shaders, Clayfield-derived simulation and fully destructible levels. Performance figures are developer claims, not reproduced here.')
source('S06','PETAFAB Steam listing','https://store.steampowered.com/app/5354780/PETAFAB_Bot_Invasion/','Listing gives November 6, 2026 release date at this check; therefore not yet a released-game validation as of October 28.')
source('S07','UE5.8 feature overview','https://www.unrealengine.com/news/unreal-engine-5-8-is-now-available','MegaLights is production-ready; Lumen Lite lowers GI cost; Mesh Terrain and Fast Geometry Streaming are experimental. Feature maturity differs across systems.')
source('S08','The road to UE6','https://www.unrealengine.com/news/the-road-to-ue-6','Epic targets end-2027 Early Access and full release 12–18 months later. Development stream is explicitly not alpha. Verse, interoperability and development workflows are major directions.',date='2026-06-22')
source('S09','Epic MegaLights technique overview','https://dev.epicgames.com/documentation/en-us/unreal-engine/megalights-in-unreal-engine','Fixed stochastic direct-light sample budget, light guiding, screen-space supplements and denoising. Lighting complexity changes quality; simplified BVH geometry can lose shadows. A direct-lighting system, not a full path tracer.')
source('S10','UE ray tracing performance guide','https://dev.epicgames.com/documentation/unreal-engine/ray-tracing-performance-guide-in-unreal-engine','Acceleration-structure updates, instance counts and ray-tracing geometry residency are independent performance costs. RT proxies and residency controls matter for dense worlds.')
source('S11','Agility SDK release table','https://devblogs.microsoft.com/directx/directx12agility/','Retail 1.619.6 listed September 14, 2026; preview 1.721.3 listed July 30, 2026. SDK availability does not imply driver/hardware feature support.')
source('S12','Shader Model 6.9 retail and more','https://devblogs.microsoft.com/directx/shader-model-6-9-retail-and-more/','Retail DXR1.2 adds SER and OMM. SM6.9 long vectors; Cooperative Vector deprecated in favor of unified SM6.10 Linear Algebra. SER API and actual reordering capability differ.',date='2026-02-26')
source('S13','DirectX Linear Algebra runtime feature support','https://microsoft.github.io/DirectX-Specs/d3d/D3D12LinearAlgebraRuntimeFeatureSupport.html','Draft v0.11: per-operation type/shape queries, native-operation enumeration, and emulated input/output distinctions. SM6.10 Linear Algebra is a preview research dependency.')
source('S14','DirectX specification status index','https://microsoft.github.io/DirectX-Specs/','Raytracing2 explicitly marked work in progress, not in preview yet. Prefer explicit maturity status over old forecast language.')
source('S15','DXR Part 2 draft','https://github.com/microsoft/DirectX-Specs/blob/master/d3d/Raytracing2.md','Draft v0.38 September 8, 2026 proposes clustered geometry, partitioned TLAS, indirect AS operations. Speculative preview schedule is not proof of a shipped SDK.',date='2026-09-08')
source('S16','HLSL SM6.6 dynamic resources','https://microsoft.github.io/DirectX-Specs/d3d/HLSL_SM_6_6_DynamicResources.html','Direct descriptor heap indexing requires appropriate root signature flags and capability support; nonuniform indexing must be annotated. This is not arbitrary GPU pointer dereference.')
source('S17','RTXDI changelog','https://github.com/NVIDIA-RTX/RTXDI/blob/'+PINS['RTXDI'][1]+'/ChangeLog.md','RTXDI 3.1 includes RR integration in FullSample, PT decorrelation and compatibility-guided neighbor selection. SDK layout/API changes require explicit version pinning.')
source('S18','RTXPT architecture and known issues','https://github.com/NVIDIA-RTX/RTXPT/blob/'+PINS['RTXPT'][1]+'/README.md','Unified PT main sample and separate incremental hybrid IntroSample. D3D12 and Vulkan available with documented Vulkan feature/validation and vendor issues. A sample is not proof of universal production support.')
source('S19','Streamline programming guide','https://github.com/NVIDIA-RTX/Streamline/blob/'+PINS['Streamline'][1]+'/docs/ProgrammingGuide.md','Initialize early; query adapter/API feature support; integration needs native resource/command objects, synchronization, resource tagging and lifetime management.')
source('S20','Streamline DLSS Ray Reconstruction guide','https://github.com/NVIDIA-RTX/Streamline/blob/'+PINS['Streamline'][1]+'/docs/ProgrammingGuideDLSS_RR.md','Noisy color, albedo, normals, roughness, dense motion, depth and reflection-motion or hit-distance guides form the integration contract. Follow pinned guide for formats and flags; do not hard-code an old depth assumption.')
source('S21','UE license','https://www.unrealengine.com/eula/unreal','UE source and Engine Tools have their own distribution conditions. Benchmarking visual goals does not make UE implementation code MIT. Marketplace asset terms are separate.',kind='license')
source('S22','ReSTIR original paper overview','https://research.nvidia.com/publication/2020-07_spatiotemporal-reservoir-resampling-real-time-ray-tracing-dynamic-direct','Resample candidates and reuse spatial/temporal samples; estimator choice changes bias. Paper benchmarks do not predict Basix speed.',date='2020-07-19',kind='research paper overview')
source('S23','RTX Mega Geometry Cluster LOD','https://github.com/NVIDIA-RTX/RTXMG/blob/main/docs/ClusterLOD.md','D3D12/Vulkan ray-traced cluster LOD path via NVRHI; follows vk_lod_clusters. Separate vendor-dependent implementation from future DXR standardization.')
source('S24','DLSS 4.5 Ray Reconstruction announcement','https://www.nvidia.com/en-us/geforce/news/gamescom-2026-nvidia-geforce-rtx-dlss-4-5-announcements/','NVIDIA describes a second-generation transformer RR model and app rollout. App model overrides and an engine SDK integration are distinct release surfaces.')
for i,(name,(repo,sha,path,license)) in enumerate(PINS.items(),25):
 source(f'S{i:02}',name+' pinned license',f'https://github.com/{repo}/blob/{sha}/{path}',f'{name} revision {sha}; {license}. Retain component-specific terms and notices.',kind='license',rights=license+'; exact SHA '+sha+'; checked '+DATE+'. '+('Preserve copyright/license notices; audit dependencies and assets separately.' if name=='NoGraphicsAPI' else 'No blanket MIT relicensing; verify redistribution scope, supplements, binaries and assets independently.'))

def cite(*ids):
 return '<span class="citations">'+ ' · '.join(f'<a href="#'+i+'">'+i+'</a>' for i in ids)+'</span>'
def table(head,rows):
 return '<div class="table-wrap"><table><thead><tr>'+''.join('<th>'+x+'</th>' for x in head)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+str(x)+'</td>' for x in r)+'</tr>' for r in rows)+'</tbody></table></div>'
sections=[]
def section(id,title,body): sections.append(dict(id=id,title=title,body=body))

section('summary','Summary and recommended direction', '''
<p class="lead">Build an MIT engine around NoGraphicsAPI’s data model, prove hardware ray tracing and reconstruction early, and grow scene detail through streaming. UE5.8 is a visual target; UE6 is a roadmap to track.</p>
<div class="callout"><strong>Recommendation · proposed architecture, not implemented</strong><p>Start with a Windows Vulkan vertical slice using a pinned NoGraphicsAPI fork. Add narrow RT and native SDK interoperability extensions. Keep original Basix renderer code under MIT. Offer NVIDIA SDK adapters as optional components with separate terms and a working non-DLSS path.</p></div>
<p>This is feasible as a focused renderer and game framework. Matching UE’s complete engine, tools and content ecosystem is a much larger effort. NoGraphicsAPI reduces binding/API complexity; it does not supply scene streaming, a material system, ray tracing, denoising or an editor. The first milestone must test the missing RT/SDK seam before major engine investment.</p>
<p><strong>Evidence boundary:</strong> public documentation, license texts and the pinned NoGraphicsAPI source were inspected. No renderer was implemented, no GPU benchmarks were run, and no visual parity has been demonstrated. All budgets and milestones below are design proposals.</p>'''+cite('S01','S02','S25','S26'))

section('research-map','Research map and decision gates',table(['ID / priority','Question and dependency','Evaluation and stopping condition'],[
 ('Q1 · High','Can the core stay MIT? Independent gate.','Inspect exact code licenses, runtime submodules and SDK exceptions. Stop when core versus optional distribution boundaries are explicit; shipping clearance stays open.'),
 ('Q2 · High','Can NoGraphicsAPI host hardware RT and DLSS? Depends Q1.','Inspect public API and shader ABI. Stop research at a concrete extension plan; capability, native-object and state restoration spikes must still run.'),
 ('Q3 · High','What yields UE-like detail? Depends Q2.','Separate geometry, materials, lighting, reconstruction and residency. Stop at a staged architecture with measurable scene tests.'),
 ('Q4 · High','MegaLights, RTXDI or full PT? Depends Q2.','Compare direct-light scope, sampling bias, temporal behavior and guides. Stop at one selectable DI strategy and two rendering modes.'),
 ('Q5 · Medium','Which DX updates can ship? Depends Q2.','Use official SDK release tables and explicit draft status. Stop when retail/preview/draft are separated and fallbacks identified.'),
 ('Q6 · Medium','What does PETAFAB demonstrate? Independent evidence.','Use author’s technology description and Steam status. Stop at supported transferable principles; do not infer undisclosed BVH, SDK or transport internals.')])+'''
<p>Scope expanded during research to include MegaLights and PETAFAB at the user’s request. Open implementation questions are retained in the roadmap and evidence library. Primary sources outrank third-party summaries; mutable pages need rechecking before dependency updates.</p>''')

section('unreal','UE5.8 and UE6: define the benchmark', '''
<p>Epic released UE5.8 in June 2026. Its rendering overview makes MegaLights production-ready and introduces lower-cost Lumen Lite GI. Geometry/worldbuilding features have separate maturity labels; do not treat everything in the release as production-ready. Epic targets UE6 Early Access at the end of 2027 and full release 12–18 months afterward. That is a target, and the development stream is not an alpha.</p>'''+cite('S07','S08')+table(['UE capability','Basix requirement / proposed equivalent','First useful scope'],[
 ('Nanite-like detail','Cluster hierarchy, LOD error bounds, GPU culling and geometry streaming; maintain a compatible RT representation.','Conventional meshlets and explicit LODs before virtualized microgeometry.'),
 ('Lumen-like GI','Dynamic bounce lighting with stable temporal reconstruction and disocclusion recovery.','Probe/cached diffuse GI for hybrid mode; independent full PT reference.'),
 ('MegaLights','Stochastic many-light direct illumination with soft visibility.','Analytic area lights plus emissive triangle sampling; inspect selected samples and visibility.'),
 ('Material richness','Consistent energy-aware BSDF evaluation and texture LOD in raster and ray paths.','Metallic/roughness, normal maps, emissive and alpha test; later layered materials and transmission.'),
 ('Large worlds','Camera-relative coordinates, cell streaming, dependency-aware asset cooking and residency.','One dense district or indoor/outdoor scene with bounded memory.'),
 ('Development speed','Deterministic asset builds, shader cache, crash diagnostics and hot reload.','Scene inspector and reproducible capture runner before a general editor.')])+'''
<p><strong>Reasoning:</strong> “UE-like” must be converted into scene-level acceptance criteria. A close-up scanned prop, an emissive city interior and a forest stress very different systems. High triangle count alone cannot establish parity. Use self-created or separately licensed assets, a fixed camera and material/light calibration; include a path-traced reference in comparisons.</p>''')

section('nographics','NoGraphicsAPI: adopt the model, expose the gaps', '''
<p>The pinned upstream implementation uses C++20 and shared Slang shaders on Vulkan and Metal. It is MIT, with separate third-party notices. Vulkan 1.4 alone is insufficient: the README requires descriptor heaps, mesh shaders and untyped shader pointers. Its compatibility table is a dated report, not a promise about every current driver. Pin the toolchain and enumerate device features on startup.</p>'''+cite('S01','S25')+'''
<p>Source inspection of <code>NoGraphicsAPI.hpp</code> at <code>ffd9f2ab…</code> finds draw/compute/mesh, heap, queue and synchronization APIs, but no public BLAS/TLAS construction, ray pipeline dispatch or native-handle SDK interface. Hardware triangle RT therefore requires implementation work. Compute dispatch can host software tracing, but does not by itself expose hardware traversal.</p>'''+cite('S02')+'''
<p>Keep the pointer-root approach, application-owned allocation and indexed texture heaps. Protect root addresses and descriptor slots until GPU completion. Roots use a shared constant-buffer ABI, while pointed-to data uses C POD layout; enforce C++ offsets and compiled shader reflection in CI. Upstream synchronization omits per-resource lists, but Basix still needs resource knowledge for lifetime, aliasing, SDK state contracts and scheduling.</p>'''+cite('S03','S04')+table(['Proposed Basix extension','Responsibilities / proof required'],[
 ('RayScene / RayBackend','BLAS/TLAS size queries, scratch allocation, build/update/compact, instance masks, AABB geometry, and build-to-trace synchronization. Trace a static triangle and a moving instance first.'),
 ('NativeInterop scope','Borrow the owning device, queues, command list/buffer and native images. Define states/layouts on entry and exit, queue ownership and SDK completion. Restore heap/pipeline bindings after SDK evaluation.'),
 ('ShaderCapabilities','Feature-selected shader variants for ray query and ray pipelines; explicit compiler and target versions. Compile probes must use the actual pinned Slang/DXC versions.'),
 ('ResourceRegistry','Track native object metadata behind GPU allocations/texture handles, including usage, aliasing, views, owner queue and retire fence.'),
 ('Failure handling','Unsupported feature diagnostics, device loss, SDK startup failure and fallback reconstruction must produce a usable error or supported rendering mode.')])+'''
<p>Maintain these as a small, tested fork or upstreamable extension rather than scattering native calls throughout the renderer. Native SDK use must share the actual graphics device and resource ownership; introducing an unrelated second device is not a transparent shortcut.</p>''')

section('petafab','PETAFAB: what the public evidence supports', '''
<p>Aaltonen’s technology page explicitly links PETAFAB to a custom NoGraphicsAPI engine, distance-field ray tracing, mesh shaders and Clayfield-derived matter/fluid simulation. It describes fully destructible SDF environments and large bot crowds. The author reports native 4K/240 FPS on RTX 4080 and other device figures; those results lack a reproduced test protocol here and are not comparable to a general triangle-based path tracer. Steam lists November 6, 2026, so the game is still upcoming at this research date.</p>'''+cite('S05','S06')+'''
<h3>Transferable lessons · analytical</h3><p>Choose the world representation around gameplay. An SDF world can make boolean destruction and ray marching natural, while mesh shaders serve other geometry workloads. A specialized representation and bounded art direction can outperform a general renderer without implying equal coverage of every material and scene type.</p>
<p>For Basix, retain triangle geometry as the general asset path. Prototype SDF volumes only if destruction is a game requirement. A hybrid could render static/animated meshes conventionally and intersect destructible SDF chunks through procedural RT primitives, with explicit bounds and conservative distance steps. This is a proposed experiment, not a disclosed PETAFAB implementation.</p>
<h3>Unanswered implementation questions</h3><p>The examined public pages do not establish its hardware RT acceleration strategy, full light-transport estimator, denoiser, DLSS integration, source-code license, SDF resolution/streaming or worst-case destruction cost. Do not infer those from “ray tracing” or from footage. SDF ray marching and DXR triangle traversal are different mechanisms; neither phrase proves full Monte Carlo path tracing.</p>''')

section('megalights','MegaLights and RTXDI: one direct-lighting problem', '''
<p>Epic describes MegaLights as stochastic direct lighting: a fixed ray/sample budget targets influential lights, followed by reconstruction. More lighting complexity can reduce quality at the same budget. Screen-space traces supplement the simplified RT scene. BVH mismatch, visibility changes and denoiser behavior remain important failure modes. UE5.8 includes fog and translucency support, but direct illumination still does not solve the entire light-transport problem.</p>'''+cite('S09')+'''
<p>RTXDI supplies reservoir-based DI/GI/PT building blocks. Its 3.1 changes include RR sample integration and PT decorrelation, showing that temporal resampling and neural reconstruction interact. The original ReSTIR work derives sample reuse estimators; unbiased and biased variants have different tradeoffs. MegaLights and RTXDI address overlapping product goals, but are not interchangeable algorithm names.</p>'''+cite('S17','S22')+table(['Choice','Role in Basix','Watch for'],[
 ('Original stochastic DI','MIT implementation from public algorithm descriptions; long-term permissive core.','Correct PDFs, candidate probabilities, visibility, reuse weights, stable light identity, disocclusion and independent review.'),
 ('Optional RTXDI adapter','Accelerated reference for resampling and high-end experiments.','Separate SDK terms, changing layouts, bridge functions and explicit ownership of reservoir resources.'),
 ('UE MegaLights reference','Visual/authoring benchmark for many soft-shadowed lights.','Do not import UE implementation code into the MIT core or assume full PT.'),
 ('Full PT mode','A different integrator using shared materials and scene data.','Avoid counting the same direct contribution in both the integrator and a separate DI pass.')])+'''
<h3>Proposed many-light implementation</h3><p>Create a stable light table for analytic lights and emissive triangles. Generate importance candidates, retain weighted samples, reproject prior reservoirs only when identity, depth, normal and material remain valid, then perform optional spatial reuse and visibility evaluation. Keep selection/PDF conventions documented and test against brute-force light summation. Treat visibility reuse and bias correction as explicit settings, not invisible quality shortcuts.</p>
<p>Expose views for selected light IDs, weights, reservoir age, rejected history, shadow rays, lighting variance and raster-versus-RT geometry. Include a sudden light toggle, fast-moving occluder, tiny bright emitter and disocclusion test. Start with opaque surfaces; add alpha-tested foliage, volumes and transmission in separate measured steps.</p>''')

section('architecture','Proposed engine and frame architecture', '''
<figure class="pipeline"><div>Assets → deterministic cooker → meshes / textures / materials / scene cells</div><div>GPU scene + residency → visibility / G-buffer + BLAS / TLAS</div><div class="branches"><span>Hybrid: stochastic DI + diffuse GI / reflections</span><span>PT: shared BSDF + light sampling + multi-bounce transport</span></div><div>Guides → fallback denoising or optional DLSS RR → post → UI → presentation</div><figcaption>Proposed data flow. Raster and ray paths share asset identity and material semantics; reconstruction owns its history.</figcaption></figure>
<p><strong>Modules:</strong> Platform (window/input/files), Core (jobs/allocators/diagnostics), Assets (import/cook/cache), World (entities/transforms/animation/streaming), Render (GPU scene/materials/frame graph), GPU (NoGraphicsAPI plus extensions), Integrations (optional SDK adapters), and Tools (inspector/capture/profiling). Keep gameplay and physics outside rendering dependencies. Choose audio, physics and asset libraries only after separate license and integration checks.</p>
<h3>Frame ownership and scheduling</h3><p>Freeze a render snapshot of scene transforms and material/light tables. Upload changes into fence-protected ranges. Schedule animation/deformation, BLAS updates and TLAS construction before rays consume them. Cull visibility with GPU work, render stable primary-surface guides, then run the selected lighting integrator. Supply the reconstruction path with an explicit camera/history epoch. Retire descriptors, memory and acceleration structures after their last consumer completes.</p>
<p>A render graph should declare reads/writes and external SDK boundaries even if the low-level API exposes global barriers. The graph can enforce ordering, transient resource reuse and memory peaks. Cross-queue overlap is a measured optimization: simultaneous AS construction, streaming and lighting may contend for bandwidth rather than shorten the frame.</p>
<h3>D3D12 decision</h3><p>A native D3D12 backend is strategically useful for PIX, DXR and Linear Algebra experiments, but it is a second implementation project. SM6.6 descriptor indexing provides bindless resources, not arbitrary shader pointer traversal. Use a logical <code>GpuRef</code> abstraction: Vulkan may encode a device pointer; D3D12 can encode a buffer/arena index plus byte offset behind typed access helpers. Shared structures must not silently reinterpret one encoding as the other.</p>'''+cite('S16')+'''
<p>Build a compile-and-dispatch ABI spike before promising one source path across DXIL/SPIR-V/Metal. Backend-specific access helpers may be necessary. If D3D12 must ship first, accept this abstraction work immediately; otherwise prove Vulkan RT/DLSS first and preserve backend-independent renderer interfaces. Do not implement a broad generic RHI before proving these two risky seams.</p>''')

section('pathtracing','RT and PT quality: establish correctness first', '''
<p>RTXPT’s main sample uses unified path tracing; its separate IntroSample demonstrates incremental hybrid rendering. It is useful as a reference for architecture, guides and validation, but its known issues distinguish D3D12 from Vulkan and include vendor-specific failures. Its framework and SDK dependency tree should not be copied wholesale into an MIT engine.</p>'''+cite('S18','S29')+'''
<h3>Proposed common foundation</h3><p>Implement one BSDF library for raster evaluation, direct-light sampling and ray hits. Begin with diffuse and GGX reflection, metallic/roughness textures, emissive surfaces, normal maps and double-sided rules. Use consistent units and exposure. Add a basic progressive reference integrator with next-event estimation, multiple importance sampling and Russian roulette; retain an accumulation mode without temporal reuse, neural reconstruction or radiance caches.</p>
<p>Validate energy conservation, PDF normalization, sampling versus evaluation, and furnace behavior. Test transmission, volumes and hair later with their own reference scenes. Use scale-aware ray origin offsets, robust intersection handling and ray-cone or differential-based texture LOD. Primary visibility must agree with the RT scene on alpha testing, LOD, deformation and material identity.</p>
<h3>Geometry is a separate budget</h3><p>Static BLAS can be built once and compacted; deforming meshes require update or rebuild policies chosen by quality and traversal cost. Refit is not always faster overall. Track scratch peaks, TLAS instance counts, animation cost and allocation churn. Simplified RT proxies can make shadows/reflections diverge from raster detail. Visualize that divergence directly and budget higher RT detail for hero objects and mirrors.</p>'''+cite('S10')+'''
<h3>Realtime evolution · proposal</h3><p>Establish a low-sample PT baseline, then add temporal denoising and optional RR. Introduce reservoir sampling, caches and SER one at a time against reference captures. Any radiance cache needs invalidation for moving geometry and lighting. Prefer predictable quality degradation and a hybrid fallback over assuming full PT will meet every device’s frame budget.</p>''')

section('dlss','DLSS: plan the resource contract now', '''
<p>Use an original Basix reconstruction interface with backend-specific adapters. Streamline’s framework is MIT with exceptions, while DLSS remains separately licensed. Initialize the integration at the required point in device startup; query each feature for the selected adapter/API. Native resource tagging, layouts/states, command ownership and completion are first-class requirements for the NoGraphicsAPI extension.</p>'''+cite('S19','S26','S31')+'''
<p>The pinned RR guide requires noisy color and material/geometric guides: diffuse/specular albedo, normals/roughness, motion, depth, and reflection motion or hit distance. Exact formats, packing and coordinate flags belong to the pinned guide. Provide dense object/camera motion, stable jitter and reset on camera cuts, resolution changes and scene epochs. RR is a reconstruction path; do not blindly add it after a separate SR invocation or pre-denoise away the signal it expects.</p>'''+cite('S20')+table(['Path','Inputs / integration order','Failure behavior'],[
 ('Fallback reconstruction','Original temporal/spatial denoising and antialiasing; same guide semantics.','Required baseline on unsupported adapters and without NVIDIA binaries.'),
 ('DLSS Super Resolution','Color, depth, motion, exposure/jitter and reset conventions. Integrate and validate before RR.','Keep native/fallback rendering selectable.'),
 ('DLSS Ray Reconstruction','Noisy transport plus material/geometric guides. Coordinate with sampling decorrelation and history.','Switch reconstruction ownership cleanly; clear incompatible history.'),
 ('Frame Generation / MFG','Later integration with supported hardware, presentation/latency contracts and UI handling.','Measure simulation/render latency and base rendered FPS independently of displayed generated frames.')])+'''
<p>NVIDIA’s 2026 announcement describes DLSS 4.5 RR model updates. Model availability through NVIDIA app overrides, an SDK release, and validated engine support are separate facts. Pin deployed binaries and record model/preset/driver during captures; do not equate a marketing generation with a universal feature set.</p>'''+cite('S24')+'''
<p><strong>Required experiments:</strong> moving reflections, thin geometry, particles, transparent glass, animated textures, disocclusion, sudden exposure changes, resizing and feature toggling. Include overlays of depth and motion vectors. A stable screenshot alone is not evidence of reconstruction quality.</p>''')

section('dx-roadmap','DX12, DXR and Linear Algebra: availability ladder', table(['Technology','Status at research date','Basix policy'],[
 ('Agility 1.619.6','Latest retail listed, 2026-09-14.','Candidate retail pin for D3D12 branch; validate runtime/compiler/driver combination.'),
 ('DXR1.2 / SM6.9','Retail since 2026-02-26; SER and OMM.','Query individual support. Keep conventional DXR1.1 path; API support does not guarantee effective hardware reordering.'),
 ('Agility 1.721.3-preview / SM6.10','Preview listed 2026-07-30; Linear Algebra.','Separate experimental configuration and shader cache namespace.'),
 ('DirectX Linear Algebra','Draft/runtime queries; vector/matrix operations with shape/type support and emulation distinctions.','Research neural shaders/caches only after renderer correctness. Conventional fallback required.'),
 ('DXR Part 2','Official status index: work in progress, not in preview yet.','Track clustered geometry, partitioned TLAS and indirect AS work. Draft forecast dates are not a shipping commitment.')])+cite('S11','S12','S13','S14','S15')+'''
<p>Linear Algebra exposes shader-side operations that can support neural workloads; it does not supply trained weights, a denoiser, a radiance cache, DLSS or a faster renderer by itself. Cooperative Vector guidance from earlier announcements is stale: Microsoft deprecated that track in favor of the unified SM6.10 design. Query operation shape/type support and distinguish native work from emulated inputs/outputs.</p>
<p><strong>Proposed research sequence:</strong> benchmark a tiny inference kernel against ordinary arithmetic, then evaluate a bounded neural material or radiance-cache experiment. Include inference, feature encoding, training/update, memory and invalidation cost. Compare image error and temporal stability with the conventional implementation under the same frame budget. Model/weight licensing is independent from the shader code license.</p>
<p>RTX Mega Geometry has a current vendor SDK cluster-LOD route on D3D12/Vulkan. This is useful evidence for streaming high-detail RT geometry, but not evidence that the future DXR2 standard is already available. Keep vendor extensions and future standard capabilities separately named.</p>'''+cite('S23'))

section('license','MIT strategy and dependency ledger', '''
<div class="callout"><strong>MIT Basix core + optional separately licensed SDKs</strong><p>This preserves an MIT license for original Basix code. A binary package containing NVIDIA SDKs is a mixed-license distribution. Optional loading is an architectural boundary, not a legal exemption.</p></div>
<p>NoGraphicsAPI’s inspected code is MIT; retain its notices and audit its dependencies/assets. Streamline’s framework is MIT, with separately licensed Nsight Perf items. The checked DLSS, RTXDI, RTXDI-Library, RTXPT and NRD revisions use NVIDIA RTX SDKs terms. None should be relabeled MIT or copied into an MIT-only vendor directory.</p>
'''+table(['Component','Pinned revision','License / proposed adoption'],[(name, '<code>'+sha+'</code>', '<a href="https://github.com/'+repo+'/blob/'+sha+'/'+path+'">'+license+'</a>') for name,(repo,sha,path,license) in PINS.items()])+'''
<p>The NVIDIA licenses contain component-specific rights and conditions for sample-source changes and incorporated object-code distribution; they do not give blanket standalone SDK sublicensing or MIT source redistribution. DLSS includes GPU scope and commercial distribution/attribution conditions. Review the exact supplements and shipping package before release. This is an engineering license assessment, not a legal clearance.</p>
<p>For a strictly permissive runtime, write original sampling/denoising implementations and provide a permissively licensed or original upscaler. Reading a paper is not a patent clearance. Keep a provenance ledger for algorithms, code, generated weights, datasets, models, textures and scenes. UE implementation code, NvRTX patches and assets retain their own terms; UE-level visual goals do not authorize relicensing them.</p>'''+cite('S21')+'''
<p><strong>Proposed build policy:</strong> the default configure/build must neither fetch nor require proprietary packages. Explicit opt-in SDK adapters use pinned manifests and retain third-party notices. A release checklist enumerates binaries and licenses actually shipped. This documentation repository currently contains no engine or NVIDIA SDK code.</p>''')

section('roadmap','Implementation roadmap and acceptance tests', '''
<p>These are ordered decision gates rather than promised dates. Team size, GPU targets and intended game genre are not yet specified. A focused vertical slice is the next investment; complete UE-class engine parity is not a credible short milestone.</p>'''+table(['Gate','Deliverable','Pass / stop criterion'],[
 ('M0 · Foundation','Pin NoGraphicsAPI, Slang and SDK manifests. Build raster/compute smoke scene and capability report.','Stable root ABI, lifetime checks and clean validation on one named target GPU/driver. Fail early with missing-extension diagnostics.'),
 ('M1 · Risk spike','Add BLAS/TLAS, hardware rays, native image/command access and one reconstruction SDK evaluation.','Moving triangle + resized window + camera cut works with correct states and no device loss. Prove same-device ownership. Reconsider backend if seam is impractical.'),
 ('M2 · Correct renderer','glTF-like scene ingestion, shared BSDF, reference PT and fallback temporal path.','Furnace/PDF checks, deterministic captures, reference convergence and motion-vector tests pass.'),
 ('M3 · Many lights','Original stochastic DI or optional RTXDI bridge; analytic and emissive lights.','Matches brute-force reference within declared error; handles moving occluders/light deletion without stale energy.'),
 ('M4 · Detail and streaming','Meshlets/LODs, texture streaming, RT proxy selection and residency telemetry.','Dense scene stays within explicit VRAM budget; memory pressure lowers detail without use-after-free or unbounded stalls.'),
 ('M5 · Reconstruction quality','SR then RR with guide inspection and temporal test clips.','No systematic ghosting/reset bug; fallback and feature toggles survive resize/device transitions. Record SDK/driver/model.'),
 ('M6 · Game slice','Input, collision/physics, animation, audio, save/load and asset tooling.','Playable encounter with packaged assets and reproducible build. Destruction/SDF only if gameplay requires it.'),
 ('M7 · Optimization','SER/OMM, caches, vendor geometry and optional D3D12/LinAlg experiments.','Each feature has isolated correctness, quality and timing evidence; unsupported hardware remains usable.')])+'''
<h3>Suggested evaluation matrix · no measurements yet</h3><p>Use an NVIDIA RTX machine for DLSS, a second non-NVIDIA RT adapter for fallback behavior, and a lower-memory device for residency pressure. Select actual models before setting performance commitments. Vulkan extension availability and DLSS feature support must be recorded per device/driver, not inferred from brand or API version.</p>
<p>Capture a Cornell-like box, many-light interior, foliage/alpha scene, glossy moving reflections, dense scanned geometry and a skinned crowd. Report internal/output resolution, sample/bounce counts, rendered versus generated FPS, median and p95 GPU frame time, CPU frame time, peak VRAM, AS build/update time, streaming stalls and error against a converged reference. Include motion clips and worst-case camera paths.</p>
<p>Illustrative planning envelope: 60 rendered FPS implies 16.67 ms total per frame. Allocate a provisional 2.5 ms geometry/AS, 2.5 ms primary visibility/materials, 5.5 ms lighting, 3.0 ms reconstruction and 1.5 ms post/UI, leaving 1.67 ms headroom. These allocations are hypothetical, hardware-dependent and must be replaced by captures; generated frames do not enlarge the simulation/render budget.</p>
<p><strong>Checks for this research delivery:</strong> validate page anchors and local links, source IDs, evidence schema, offline search coverage, mobile layout and both themes. These documentation checks do not validate rendering feasibility. See <a href="verification.json">verification receipt</a> for the checks actually executed.</p>''')

section('lessons','Summary and lessons learned', '''
<p><strong>What mattered:</strong> the API abstraction and the renderer are different projects. NoGraphicsAPI provides a compact model, but hardware RT and SDK interoperability are missing public seams. PETAFAB supports the value of specialization; it does not establish general-purpose triangle PT performance. UE’s many-light quality also depends on scene representation and reconstruction.</p>
<p><strong>Practical consequence:</strong> test RT → native resource tagging → reconstruction before building an editor or virtualized geometry system. Share material/scene semantics between hybrid and PT paths. Treat geometry residency, sample selection and temporal quality as independent budgets.</p>
<p><strong>License consequence:</strong> the requested MIT core is practical, but the requested NVIDIA stack cannot all become MIT. Preserve a functional default without proprietary SDKs and publish the exact dependency/license manifest for each optional distribution.</p>
<p><strong>Limits:</strong> no local rendering experiments, cross-vendor benchmarks or shipping legal review were performed. DXR2/UE6 dates and preview APIs may change. The first next step is M0/M1, followed by a decision on Windows/Vulkan-first versus immediate D3D12 investment.</p>''')

import research_expansion
research_expansion.apply(sections,sources,source,section,table,cite)
import thread_research
thread_research.apply(sections,sources,source,section,table,cite)
import sdk_status
sdk_status.apply(sections,sources,source,section,table,cite)
import vulkan_research, reference_status
vulkan_research.apply(sections,sources,source,section,table,cite)
reference_status.apply(sections,sources,source,section,table,cite)
import gpu_transport_status
gpu_transport_status.apply(sections,sources,source,section,table,cite)
import vndf_status
vndf_status.apply(sections,sources,source,section,table,cite)
import ray_spawn_status
ray_spawn_status.apply(sections,sources,source,section,table,cite)
import ray_boundary_status
ray_boundary_status.apply(sections,sources,source,section,table,cite)
import shared_instance_status
shared_instance_status.apply(sections,sources,source,section,table,cite)

section('sources','Evidence, sources and research maintenance','''<p>Checked 28 October 2026. Source records distinguish code inspection, vendor documentation, licenses and research. External links need a network connection; this page, local search and the JSON/SQLite evidence work offline. Source snapshots are pinned where available; roadmap pages remain mutable.</p>'''+''.join('<article id="'+s['id']+'"><h3>'+s['id']+' · <a href="'+html.escape(s['url'],quote=True)+'">'+html.escape(s['title'])+'</a></h3><p>'+html.escape(s['notes'])+'</p><p class="doc-caption">'+html.escape(s['kind']+' · checked '+DATE+' · '+s['rights'])+'</p></article>' for s in sources)+'''<p><a href="evidence/library.json">Structured library</a> · <a href="evidence/index.sqlite">SQLite full-text index</a> · <a href="dependencies.json">Dependency pins</a></p><p class="doc-caption">Changelog · 2026-10-28: initial research, architecture proposal, MegaLights comparison, PETAFAB case study, license boundary and validation plan.</p>''')

def main():
 DOC.mkdir(parents=True,exist_ok=True)
 from roadmap import load as load_roadmap, markdown as roadmap_markdown
 roadmap_data=load_roadmap(check_paths=True)
 (DOC/'roadmap-status.json').write_text(json.dumps(roadmap_data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
 (ROOT/'ENGINE_STATUS.md').write_text(roadmap_markdown(roadmap_data),encoding='utf-8')
 nav=''.join('<a href="#'+s['id']+'"><span class="doc-nav-number">'+f'{i:02}'+'</span> '+s['title']+'</a>' for i,s in enumerate(sections,1))
 body=''.join('<section class="doc-panel" id="'+s['id']+'"><h2>'+s['title']+'</h2>'+s['body']+'</section>' for s in sections)
 page='''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Basix Engine · Rendering research</title><link rel="icon" href="identity.svg"><link rel="stylesheet" href="shell.css"><link rel="stylesheet" href="theme.css"><link rel="stylesheet" href="basix.css"><script src="theme.js"></script><script defer src="shell.js"></script></head><body><a class="doc-skip" href="#main">Skip to content</a><header class="doc-header"><a class="doc-brand" href="index.html"><img src="identity.svg" alt=""><span><strong>Basix Engine</strong><small>Rendering research · 28 Oct 2026</small></span></a><nav class="doc-actions" aria-label="Page actions"><a href="#summary">Overview</a><button data-doc-back type="button">Back</button><a href="https://github.com/BasixMaster/Basix_Engine">GitHub</a></nav><label class="doc-search">Search all docs<input data-doc-search type="search" placeholder="MegaLights, license, DXR…" aria-controls="doc-results"></label><div data-doc-theme-host></div></header><div class="doc-layout"><aside class="doc-sidebar"><details open><summary>01 · Research chapters</summary><nav aria-label="On this page">'''+nav+'''</nav></details></aside><main id="main" class="doc-main"><div class="hero"><img class="hero-art" src="banner.svg" alt="" aria-hidden="true"><p>RESEARCH / ARCHITECTURE / DEVELOPMENT</p><h1>A small GPU API.<br>A detailed world.</h1><p>Designing an MIT game engine for rich geometry, many lights and robust ray tracing.</p><div class="tags"><span>UE5.8 / UE6</span><span>Vulkan / DX12</span><span>RT / PT / DLSS</span><span>MIT core</span></div></div><div id="doc-results" class="doc-results" hidden></div>'''+body+'''<footer class="doc-footer">Original Basix documentation under MIT. Third-party source material and SDKs retain their own terms.</footer></main></div></body></html>'''
 (DOC/'index.html').write_text(page,encoding='utf-8')
 pins={name:dict(repository=repo,revision=sha,license=lic,license_url=f'https://github.com/{repo}/blob/{sha}/{path}',adoption='research-only; not vendored') for name,(repo,sha,path,lic) in PINS.items()}
 (DOC/'dependencies.json').write_text(json.dumps(dict(checked_on=DATE,components=pins),indent=2)+'\n',encoding='utf-8')
 questions=[]
 for n,title in enumerate(['MIT distribution boundary','Hardware RT and SDK interop','UE-like scene detail','Many-light and PT architecture','DX roadmap maturity','PETAFAB evidence'],1):
  questions.append(dict(id='Q'+str(n),title=title,topic='Basix engine',priority='high' if n<5 else 'medium',status='complete',why='Resolve a consequential architecture decision before engine implementation.',depends_on=[] if n in [1,6] else ['Q1'],search_terms=[title],source_types=['primary documentation','code','license'],evaluation='Compare exact implementation, explicit maturity and distribution terms; distinguish proposal from measurement.',stop_when='A bounded decision and executable validation gate are documented.',coverage='Research decision covered; no runtime or shipping validation. See research-map chapter and milestones.'))
 findings=[]
 for n,(title,claim,ids,q) in enumerate([
  ('MIT core boundary','Original MIT core can coexist with optional separately licensed SDKs; the combined NVIDIA stack is not MIT.',['S25','S26','S27','S28','S29','S30','S31'],'Q1'),
  ('Missing hardware RT API','Inspected NoGraphicsAPI public header has no AS construction, ray dispatch or native SDK object interface.',['S02'],'Q2'),
  ('Many-light scope','MegaLights solves direct illumination; it is not a synonym for full PT or RTXDI.',['S09','S17'],'Q4'),
  ('Roadmap distinction','DXR1.2 is retail; LinAlg is preview; DXR2 status is work in progress; UE6 is a future target.',['S08','S11','S12','S13','S14'],'Q5'),
  ('PETAFAB specialization','Author describes NoGraphicsAPI with distance-field tracing and mesh shaders; this does not establish general triangle-PT throughput.',['S05','S06'],'Q6')],1):
  findings.append(dict(id='F'+str(n),title=title,question_ids=[q],topic='Rendering architecture',claim=claim,evidence_type='reported' if n!=2 else 'analytical',confidence='high',context='Public source review as of '+DATE,strengths='Direct primary-source traceability.',limitations='No execution, cross-vendor test or independent performance measurement.',implications='Follow the associated chapter and milestone gate before implementation.',tags=['Basix','rendering','research'],evidence=[dict(source_id=i,locator='Linked document and source notes',relation='supports',note='Supports the scoped claim, not a Basix runtime result.') for i in ids]))
 library=dict(schema_version=1,title='Basix Engine evidence library',purpose='Support MIT engine architecture and future rendering implementation.',scope='UE5.8/6, NoGraphicsAPI, PETAFAB, DXR/DX12, Linear Algebra, RTXDI/RTXPT and DLSS.',updated_on=DATE,summary=['Research only; implementation proposals and vendor claims are distinguished from measurements.'],questions=questions,sources=sources,findings=findings,actions=[dict(id='A1',title='Implement M0/M1 risk spike',priority='high',finding_ids=['F1','F2'],rationale='Prove the missing hardware RT and reconstruction seam before broad engine work.',prerequisites='Choose GPU/driver targets and optional SDK terms.',success_criterion='Hardware rays and reconstruction run with correct lifetimes, resize/reset handling and fallback.')],search_log=[dict(date=DATE,phase='scout',engine='Official web sources and GitHub',query='UE5.8 UE6 DirectX Linear Algebra NoGraphicsAPI RTXDI RTXPT licenses',source_ids=[s['id'] for s in sources],outcome='Primary-source architecture and license gates resolved; runtime tests remain pending.'),dict(date=DATE,phase='deep',engine='Local code and official developer pages',query='NoGraphicsAPI native interop RT header PETAFAB MegaLights',source_ids=['S02','S03','S04','S05','S06','S09'],outcome='Header inspected locally; PETAFAB internals beyond public description remain unknown.')],scope_changes=['Added MegaLights and PETAFAB at user request.'])
 evidence=DOC/'evidence';evidence.mkdir(exist_ok=True)
 research_expansion.enrich_library(library)
 thread_research.enrich_library(library)
 sdk_status.enrich_library(library)
 vulkan_research.enrich_library(library)
 reference_status.enrich_library(library)
 gpu_transport_status.enrich_library(library)
 vndf_status.enrich_library(library)
 ray_spawn_status.enrich_library(library)
 ray_boundary_status.enrich_library(library)
 shared_instance_status.enrich_library(library)
 library.update(presentation_path='index.html',evidence_page='index.html#sources',guide_pages=['index.html'],hub_topics=[dict(directory='basix-engine-rendering-research',title='Basix Engine rendering research',purpose='MIT game engine architecture, hardware ray tracing, many-light sampling and reconstruction.',topics=['rendering'],page='index.html',guide_pages=['index.html'],specialty=True)])
 for record in library['sources']+library['findings']:
  record['topic']='rendering'
  record['presentation_path']='index.html'
  record['presentation_anchor']=record['id'] if record['id'].startswith('S') else 'summary'
 for record in library['questions']+library['actions']:
  record['presentation_path']='index.html'
  record['presentation_anchor']='research-map' if record['id'].startswith('Q') else 'roadmap'
 payload=json.dumps(library,ensure_ascii=False,indent=2)+'\n'
 (evidence/'library.json').write_text(payload,encoding='utf-8')
 (DOC/'library.json').write_text(payload,encoding='utf-8')
 with sqlite3.connect(evidence/'index.sqlite') as db:
  expected=[]
  for kind in ['questions','sources','findings','actions']:
   expected.extend((r['id'],kind,r['title'],json.dumps(r,ensure_ascii=False)) for r in library[kind])
  try: existing=db.execute('SELECT id,kind,title,body FROM records ORDER BY rowid').fetchall()
  except sqlite3.DatabaseError: existing=None
  if existing!=expected:
   db.execute('DROP TABLE IF EXISTS records'); db.execute('CREATE VIRTUAL TABLE records USING fts5(id UNINDEXED,kind UNINDEXED,title,body)')
   db.executemany('INSERT INTO records VALUES (?,?,?,?)',expected)
 print(f'Built {len(sections)} chapters, {len(sources)} sources and searchable evidence.')
if __name__=='__main__':
 import argparse, importlib.util
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--private-meter',action='store_true',help='Attach the registered local meter; exclude this private build from exports.')
 args=parser.parse_args()
 main()
 if args.private_meter:
  adapter=next((p/'shared-performance-cost-meter/integration.py' for p in ROOT.parents if (p/'shared-performance-cost-meter/integration.py').is_file()),None)
  if adapter is None: raise SystemExit('No local shared meter adapter found. Meter not attached.')
  spec=importlib.util.spec_from_file_location('basix_private_meter',adapter)
  mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
  page=DOC/'index.html'
  if mod.resolve_binding(page) is None: raise SystemExit('No registered live snapshot found. Meter not attached.')
  page.write_text(mod.decorate(page.read_text(encoding='utf-8'),page),encoding='utf-8')
  print('Attached private chat meter; exclude private blocks from publication.')

