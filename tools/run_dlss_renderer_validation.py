"""Validate renderer-fed dynamic optional SR and real bilinear fallback locally.

Uses explicitly supplied pinned SDK/tools. Raw frames and full process/SDK logs
stay local. Lifecycle/liveness correctness does not establish temporal quality.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import json
import math
import os
import re
import subprocess
import sys
import time

from reconstruction_guides_oracle import verify_dynamic_receipt
from run_visible_validation import scan,sha

ROOT=Path(__file__).resolve().parents[1]
HOOK=re.compile(r'\[Streamline:1\] \[[0-9:-]+\]\[streamline\]\[warn\]\[tid:\d+\]\[[^\]]+\]pluginManager\.cpp:\d+\[processPluginHooks\] Hook sl\.common:Vulkan:(CmdBindPipeline|CmdBindDescriptorSets|BeginCommandBuffer) is NOT supported, plugin will not function properly')


def require(ok,why):
    if not ok:raise ValueError(why)


def process_diagnostics(log,name,trace=False,expect_sdk=False,forbid_sdk=False):
    require('[Streamline:2]' not in log and not re.search(r'\[streamline\]\[error\]',log,re.I),name+': SDK error diagnostic')
    hooks=[];rest=[]
    for line in log.splitlines():
        if '[Streamline:1]' in line or re.search(r'\[streamline\]\[warn\]',line,re.I):
            match=HOOK.fullmatch(line.strip());require(match is not None,name+': unreviewed SDK warning')
            hooks.append(match.group(1))
        else:rest.append(line)
    require(len(hooks)==len(set(hooks)) and len(hooks)<=3,name+': repeated SDK manual-hook warnings')
    if expect_sdk:require(set(hooks)=={'CmdBindPipeline','CmdBindDescriptorSets','BeginCommandBuffer'},name+': pinned SDK hook diagnostic catalog differs')
    if forbid_sdk:require(not re.search(r'\[Streamline:|\[streamline\]',log,re.I) and not hooks,name+': forced fallback loaded/logged SDK')
    reviewed=scan('\n'.join(rest),name,trace)
    return dict(reviewed_manual_hook_warnings=len(hooks),reviewed_loader_implicit_disable_messages=reviewed)


def contract(result,numeric,forced=False,injected=False,validation=True):
    expected=dict(status='passed',forced_fallback=forced,injected_nonprojectable_history=injected,sdk_loaded=not forced,frames=11,
                  sdk_evaluations=0 if forced else 10,fallback_frames=11 if forced else 1,
                  feature_frees=0 if forced else 3,reset_evaluations=0 if forced else 5 if injected else 4,
                  nonprojectable_history_resets=1 if injected else 0,validation_enabled=validation,validation_errors=0,
                  validation_warnings=0,sdk_teardown_errors=0)
    require(all(type(result.get(k)) is type(v) and result[k]==v for k,v in expected.items()),'Dynamic SR lifecycle/validation contract differs')
    require(numeric.get('numeric_readback_rechecked') is True and numeric.get('frames')==11,'Independent raw frame verification absent')
    require(numeric.get('injected_nonprojectable_history') is injected,'Independent injected-history profile differs')
    for key in ('guide_pixel_checks','input_component_checks','output_component_checks','fallback_component_checks'):
        require(result[key]==numeric[key],'Host/independent '+key+' differs')
    if not forced:require(result['guide_pixel_checks']==315216 and result['input_component_checks']==1891296,'Fixed pinned SDK dynamic input coverage differs')
    require(result['output_component_checks']==2204928,'Output coverage differs')
    require(math.isfinite(result['worst_depth_error']) and 0<=result['worst_depth_error']<2e-5 and math.isfinite(result['worst_motion_error_pixels']) and 0<=result['worst_motion_error_pixels']<.002,'Host immutableWorld guide oracle exceeds frozen tolerances')
    for row in result['sequence']:
        require(row['guide_failed_pixels']==0 and 0<=row['depth_max_error']<2e-5 and 0<=row['motion_max_error_pixels']<.002,'Per-frame CPU guide failure')
    return dict(frames=11,sdk_evaluations=result['sdk_evaluations'],fallback_frames=result['fallback_frames'],guide_pixel_checks=numeric['guide_pixel_checks'],input_component_checks=numeric['input_component_checks'],output_component_checks=numeric['output_component_checks'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','runtime','slangc','validation-layer-dir','shader-dir','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--release-exe',type=Path);args=p.parse_args()
    exe,runtime,slangc,layer,shaders,out=(v.resolve() for v in (args.exe,args.runtime,args.slangc,args.validation_layer_dir,args.shader_dir,args.output))
    release=args.release_exe.resolve() if args.release_exe else None
    require(not out.exists() or out.is_dir() and not any(out.iterdir()),'Use fresh empty output');out.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies'];sdk=lock['Streamline-SR']
    inputs=dict(executable=exe,slangc=slangc,validation_dll=layer/'VkLayer_khronos_validation.dll',validation_manifest=layer/'VkLayer_khronos_validation.json',harness=Path(__file__).resolve(),oracle=ROOT/'tools/reconstruction_guides_oracle.py',scanner=ROOT/'tools/run_visible_validation.py')
    if release:inputs['release_executable']=release
    require(runtime.is_dir() and {v.name.lower() for v in runtime.glob('*.dll')}=={v.lower() for v in sdk['runtime_sha256']},'Use reviewed minimal pinned runtime; extra DLL plugins are not accepted')
    for name,expected in sdk['runtime_sha256'].items():
        path=runtime/name;require(path.is_file() and sha(path)==expected,'SDK runtime hash differs: '+name);inputs['sdk_dll_'+name]=path
    caches=[parent/'CMakeCache.txt' for parent in (exe.parent,exe.parent.parent)]
    cache=next((v for v in caches if v.is_file()),None);require(cache is not None,'Build CMake cache needed to pin supplied SDK headers')
    match=re.search(r'^BASIX_STREAMLINE_SDK:PATH=(.+)$',cache.read_text(encoding='utf-8'),re.M);require(match is not None,'Configured SDK header source absent')
    sdk_root=Path(match.group(1).strip());inputs['build_cache']=cache
    for name,expected in sdk['header_sha256'].items():
        path=sdk_root/'include'/name;require(path.is_file() and sha(path)==expected,'SDK header differs: '+name);inputs['sdk_header_'+name]=path
    spvs=sorted(shaders.glob('*.spv'));require({'reconstruction_guides.spv','reconstruction_upload.spv'}<={v.name for v in spvs},'Required guide/upload shaders absent')
    for path in spvs:inputs['shader_'+path.name]=path
    for name in ('dependencies.lock.json','CMakeLists.txt','src/progressive_gpu.cpp','src/dlss_renderer_main.inl','src/dlss_vulkan_support.inl','src/streamline_runtime.cpp',
                 'src/progressive_shared_tests.inl','src/progressive_boundary_tests.inl','src/progressive_ray_tests.inl','src/progressive_transform_tests.inl',
                 'src/reconstruction_guides.cpp','include/basix/reconstruction_guides.hpp','tests/reconstruction_guides_tests.cpp','include/basix/gpu_guides_abi.hpp',
                 'src/instances.cpp','include/basix/instances.hpp','src/transform_uncertainty.cpp','include/basix/transform_uncertainty.hpp',
                 'src/reference.cpp','include/basix/reference.hpp','src/material.cpp','src/material_vndf.cpp','include/basix/material.hpp','include/basix/material_vndf.hpp',
                 'src/ray_spawn.cpp','include/basix/ray_spawn.hpp','include/basix/progressive_gpu_abi.hpp','include/basix/streamline_runtime.hpp',
                 'shaders/reconstruction_guides.slang','shaders/reconstruction_upload.slang','shaders/progressive_pt.slang','shaders/basix_ray_spawn.slang','shaders/basix_material.slang','shaders/basix_material_vndf.slang'):
        inputs[name]=ROOT/name
    require(all(v.is_file() for v in inputs.values()),'Frozen dynamic input/source missing')
    require(sha(inputs['validation_dll'])==lock['Vulkan-ValidationLayers']['dll_sha256'],'Validation DLL differs from pin')
    manifest=json.loads(inputs['validation_manifest'].read_text(encoding='utf-8'))['layer']
    require(manifest['name']=='VK_LAYER_KHRONOS_validation' and manifest['api_version']==lock['Vulkan-ValidationLayers']['manifest_api'] and (layer/manifest['library_path']).resolve()==inputs['validation_dll'],'Validation manifest/DLL mismatch')
    version=subprocess.run([str(slangc),'-version'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=30);(out/'slang-version.log').write_text(version.stdout,encoding='utf-8');version_match=re.search(r'\b(\d+\.\d+\.\d+)\b',version.stdout)
    require(not version.returncode and version_match and version_match.group(1)==lock['Slang']['version'],'Slang version differs from pin')
    before={k:sha(v) for k,v in inputs.items()};env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1');cases=[];results={};numeric={}
    missing=out/'runtime-deliberately-absent';require(not missing.exists() and missing.is_absolute(),'Missing runtime control precondition differs')

    def run(name,flags=(),marker='',forced=False,injected=False,trace=False,is_release=False,validation=True):
        target=out/name;target.mkdir();command=[str(release if is_release else exe),'--runtime',str(missing if forced else runtime),'--shader-dir',str(shaders),'--output',str(target),*flags]
        if validation:command+=['--validate']
        local=dict(env);local.pop('VK_LOADER_DEBUG',None)
        if not validation:local.pop('VK_VALIDATION_VALIDATE_SYNC',None)
        if trace:local['VK_LOADER_DEBUG']='layer'
        start=time.perf_counter();process=subprocess.run(command,env=local,cwd=target,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=1200);elapsed=time.perf_counter()-start
        (target/'process.log').write_text(process.stdout,encoding='utf-8');require(process.returncode==(1 if marker else 0),name+': unexpected process exit')
        diagnostics=process_diagnostics(process.stdout,name,trace,expect_sdk=not forced and not(is_release and marker),forbid_sdk=forced)
        if marker:
            require(marker in process.stdout,name+': expected failure missing')
            if '--fail-shutdown' in flags:
                require('injected SDK shutdown failure' in process.stdout and 'Retained native device for SDK shutdown recovery' in process.stdout,name+': injected shutdown did not retain native ownership')
        else:
            raw=target/'dynamic-result.json';result=json.loads(raw.read_text(encoding='utf-8'));checked=verify_dynamic_receipt(raw);contract(result,checked,forced,injected,validation);results[name]=result;numeric[name]=checked
            require(('PASS renderer-fed dynamic SR:11frames SDK=0 fallback=11' if forced else 'PASS renderer-fed dynamic SR:11frames SDK=10 fallback=1') in process.stdout,name+': success diagnostic missing')
        cases.append(dict(name=name,exit_code=process.returncode,expected_outcome_verified=True,process_elapsed_seconds=elapsed,validation_requested=validation,synchronization_validation_requested=validation,**diagnostics))

    run('candidate',trace=True)
    run('nonprojectable-history-reset',('--invalidate-history',),injected=True)
    run('forced-fallback',('--force-fallback',),forced=True)
    require(not missing.exists(),'Forced fallback touched missing SDK directory')
    run('omitted-evaluation',('--omit-evaluation',),'Dynamic SR output sentinel/nonfinite/range failure')
    run('post-evaluation-cleanup',('--fail-after-evaluation',),'injected dynamic SR evaluation cleanup failure')
    run('shutdown-retains-native',('--fail-shutdown',),'Dynamic SR validation/teardown diagnostics')
    if release:
        run('release-candidate',is_release=True,validation=False)
        run('release-refuses-validation',marker='--validate requires a Debug build',is_release=True)
    require(before=={k:sha(v) for k,v in inputs.items()} and [v.name for v in spvs]==[v.name for v in sorted(shaders.glob('*.spv'))],'Frozen dynamic inputs changed during validation')
    receipt=dict(checked_on='2026-10-28',status='passed',harness_checks_passed=True,cases=cases,results=results,independent_numeric=numeric,input_sha256=before,input_hashes_unchanged=True,
                 runtime_version=sdk['version'],runtime_sha256=sdk['runtime_sha256'],slang_version=version_match.group(1),loader_confirmed_validation=True,debug_synchronization_validation_requested=True,
                 sdk_warning_scope='Only three exact pinned manual-hook state-tracking warnings; no ignored-plugin exceptions or SDK errors',full_process_diagnostics='All merged process output scanned through teardown; exact loader implicit-disable notices counted separately',
                 limits=['One local device/driver and fixed synchronized two-instance sequence; no async or device-loss recovery proof',
                         'Nonprojectable history and skipped-first-shutdown recovery use explicit engine injections; actual vendor/driver failures remain unproven',
                         'Release positive is checked without validation; Debug validation coverage does not imply Release validation or synchronization validation',
                         'SDK output finite/live/mean/variation gates establish lifecycle liveness, not accepted temporal quality or DLSS reconstruction accuracy',
                         'Fallback every RGB component independently checked against uploaded half-float bilinear resampling',
                         'No RR,FG,performance,shipping-license or network-offline claim; NGX bootstrap updater may run despite disabled OTA flags'])
    (out/'dlss-renderer-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(status='passed',harness_checks=len(cases),positive_sequences=len(results),candidate_sdk_evaluations=10,forced_fallback_frames=11,candidate_guide_pixels=numeric['candidate']['guide_pixel_checks'])))


if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError) as e:
        print(str(e),file=sys.stderr);raise SystemExit(1)
