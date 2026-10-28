"""Canonical local GPU guide acceptance: candidate, five faults, compiled no-op.

No SDK evaluation. Full logs, raw96-byte records and artifacts remain local in
--output; reconstruction-guides-results.json is a path-free acceptance receipt.
"""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys
import time

from reconstruction_guides_oracle import verify_receipt, SIZES
from run_visible_validation import scan, sha

ROOT=Path(__file__).resolve().parents[1]
FAULTS=((1,'negate-motion'),(2,'double-motion'),(3,'constant-depth'),(4,'camera-only-motion'),(5,'omit'))


def require(ok,why):
    if not ok:raise ValueError(why)


def contract(result,numeric,fault=0,all_failed=False):
    expected=dict(status='failed' if fault else 'passed',frames=8,pixels=20304,fault=fault,
                  validation_enabled=True,validation_errors=0,validation_warnings=0,
                  guide_root_bytes=128,guide_pixel_bytes=96)
    require(all(type(result.get(k)) is type(v) and result[k]==v for k,v in expected.items()),'Guide metadata/validation/expected outcome differs')
    rows=result['sequence'];require(len(rows)==8,'Guide sequence length differs')
    for key in ('hits','misses','valid_motions','failed_pixels'):
        require(type(result.get(key)) is int and 0<=result[key]<=20304 and result[key]==sum(r[key] for r in rows),'Host guide '+key+' accounting differs')
    require(numeric.get('numeric_readback_rechecked') is True and numeric.get('pixels')==20304,'Independent raw readback gate missing')
    if fault:
        require(result['failed_pixels']>0 and numeric['failed_pixels']>0,'Host and independent checker must both reject fault')
        if all_failed:require(result['failed_pixels']==numeric['failed_pixels']==20304,'Omitted/no-op dispatch must reject every pixel')
    else:
        require(result['failed_pixels']==numeric['failed_pixels']==0,'Candidate contains rejected pixels')
        require(result['hits']+result['misses']==20304 and result['hits']>0 and result['misses']>0 and result['valid_motions']>0,'Candidate lacks hit/miss/history observations')
    for frame,row in enumerate(rows):
        require(all(type(row[k]) is int and 0<=row[k]<=row['pixels'] for k in ('hits','misses','valid_motions','failed_pixels')),'Invalid per-frame host counts')
        for key in ('depth_max_error','motion_max_error_pixels'):
            require(type(row.get(key)) in (int,float) and math.isfinite(row[key]) and row[key]>=0,'Invalid host error metric')
        if not fault:
            require(row['depth_max_error']<2e-5 and row['motion_max_error_pixels']<.002,'CPU World guide error exceeds frozen acceptance')
    return dict(host_failed_pixels=result['failed_pixels'],independent_failed_pixels=numeric['failed_pixels'],
                rejection_counts_recorded_separately=True)


def hdr_artifacts(directory):
    records=[]
    for frame,(w,h) in enumerate(SIZES):
        path=directory/('frame-'+str(frame))/'linear.pfm';data=path.read_bytes();header=f'PF\n{w} {h}\n-1.0\n'.encode('ascii')
        require(data.startswith(header) and len(data)==len(header)+w*h*12,'Guide color artifact dimensions/header differ')
        values=struct.unpack('<'+str(w*h*3)+'f',data[len(header):]);require(all(math.isfinite(v) and v>=0 for v in values) and min(values)!=max(values),'Guide color artifact nonfinite/negative/constant')
        records.append(dict(frame=frame,width=w,height=h,finite_components=len(values),image_sha256=sha(path)))
    return records


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('exe','shader','slangc','validation-layer-dir','output'):p.add_argument('--'+key,type=Path,required=True)
    args=p.parse_args();exe,shader,slangc,layer,out=(v.resolve() for v in (args.exe,args.shader,args.slangc,args.validation_layer_dir,args.output))
    require(not out.exists() or out.is_dir() and not any(out.iterdir()),'Use fresh empty output');out.mkdir(parents=True,exist_ok=True)
    lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
    inputs=dict(executable=exe,shader=shader,slangc=slangc,validation_dll=layer/'VkLayer_khronos_validation.dll',validation_manifest=layer/'VkLayer_khronos_validation.json',harness=Path(__file__).resolve(),oracle=ROOT/'tools/reconstruction_guides_oracle.py',scanner=ROOT/'tools/run_visible_validation.py')
    for name in ('dependencies.lock.json','CMakeLists.txt','src/progressive_gpu.cpp','src/dlss_renderer_main.inl','src/dlss_vulkan_support.inl','src/streamline_runtime.cpp',
                 'src/progressive_shared_tests.inl','src/progressive_boundary_tests.inl','src/progressive_ray_tests.inl','src/progressive_transform_tests.inl',
                 'src/reconstruction_guides.cpp','include/basix/reconstruction_guides.hpp','tests/reconstruction_guides_tests.cpp','include/basix/gpu_guides_abi.hpp',
                 'src/instances.cpp','include/basix/instances.hpp','src/transform_uncertainty.cpp','include/basix/transform_uncertainty.hpp',
                 'src/reference.cpp','include/basix/reference.hpp','src/material.cpp','src/material_vndf.cpp','include/basix/material.hpp','include/basix/material_vndf.hpp',
                 'src/ray_spawn.cpp','include/basix/ray_spawn.hpp','include/basix/progressive_gpu_abi.hpp','include/basix/streamline_runtime.hpp',
                 'shaders/reconstruction_guides.slang','shaders/progressive_pt.slang','shaders/basix_ray_spawn.slang','shaders/basix_material.slang','shaders/basix_material_vndf.slang'):
        inputs[name]=ROOT/name
    require(shader.name=='reconstruction_guides.spv' and all(v.is_file() for v in inputs.values()),'Guide input/source dependency missing')
    require(sha(inputs['validation_dll'])==lock['Vulkan-ValidationLayers']['dll_sha256'],'Validation DLL differs from pin')
    manifest=json.loads(inputs['validation_manifest'].read_text(encoding='utf-8'))['layer']
    require(manifest.get('name')=='VK_LAYER_KHRONOS_validation' and manifest.get('api_version')==lock['Vulkan-ValidationLayers']['manifest_api'] and (layer/manifest['library_path']).resolve()==inputs['validation_dll'],'Validation manifest does not select pinned supplied DLL')
    version=subprocess.run([str(slangc),'-version'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=30)
    (out/'slang-version.log').write_text(version.stdout,encoding='utf-8');match=re.search(r'\b(\d+\.\d+\.\d+)\b',version.stdout)
    require(not version.returncode and match and match.group(1)==lock['Slang']['version'],'Slang version differs from pin')
    before={k:sha(v) for k,v in inputs.items()};env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1')
    cases=[];results={};numeric={};hdr=[]

    def run(name,shader_dir,fault=0,trace=False,all_failed=False):
        nonlocal hdr
        local=dict(env);local.pop('VK_LOADER_DEBUG',None)
        if trace:local['VK_LOADER_DEBUG']='layer'
        target=out/name;command=[str(exe),'--guides-only','--validate','--shader-dir',str(shader_dir),'--output',str(target)]
        if fault:command+=['--guide-fault',str(fault)]
        start=time.perf_counter();process=subprocess.run(command,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=600);elapsed=time.perf_counter()-start
        (out/(name+'.log')).write_text(process.stdout,encoding='utf-8');require(process.returncode==(1 if fault else 0),name+': unexpected process exit')
        reviewed=scan(process.stdout,name,trace);raw=target/'guide-result.json';result=json.loads(raw.read_text(encoding='utf-8'));checked=verify_receipt(raw,require_passed=not bool(fault));counts=contract(result,checked,fault,all_failed)
        marker='FAIL: GPU renderer guide oracle failed' if fault else 'PASS GPU renderer guides:8frames 20304 pixels';require(marker in process.stdout,name+': expected acceptance/rejection diagnostic missing')
        if not fault:hdr=hdr_artifacts(target)
        results[name]=result;numeric[name]=checked;cases.append(dict(name=name,exit_code=process.returncode,expected_outcome_verified=True,process_elapsed_seconds=elapsed,reviewed_loader_implicit_disable_messages=reviewed,**counts))

    run('candidate',shader.parent,trace=True)
    for fault,label in FAULTS:run('fault-'+label,shader.parent,fault,all_failed=fault==5)
    scratch=out/'no-op-shader';scratch.mkdir();source=scratch/'no-op.slang';source.write_text('// MIT actual dispatched poisoned-output control.\n[numthreads(8,8,1)] void guideMain(uint3 id:SV_DispatchThreadID) {}\n',encoding='utf-8');target=scratch/'reconstruction_guides.spv'
    compiled=subprocess.run([str(slangc),str(source),'-entry','guideMain','-stage','compute','-target','spirv','-profile','spirv_1_5','-emit-spirv-directly','-fvk-use-entrypoint-name','-fvk-use-c-layout','-matrix-layout-row-major','-fp-mode','precise','-o',str(target)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=60)
    (out/'no-op-compile.log').write_text(compiled.stdout,encoding='utf-8');require(not compiled.returncode and target.is_file() and sha(target)!=sha(shader),'Distinct no-op shader compilation failed')
    no_op_hash,no_op_source_hash=sha(target),sha(source)
    run('compiled-no-op',scratch,1,all_failed=True)
    require(before=={k:sha(v) for k,v in inputs.items()} and sha(target)==no_op_hash and sha(source)==no_op_source_hash,'Frozen guide inputs changed during execution')
    receipt=dict(checked_on='2026-10-28',status='passed',harness_checks_passed=True,cases=cases,results=results,independent_numeric=numeric,hdr=hdr,
                 input_sha256=before,input_hashes_unchanged=True,no_op_shader_sha256=no_op_hash,no_op_source_sha256=no_op_source_hash,slang_version=match.group(1),loader_confirmed_validation=True,synchronization_validation_requested=True,
                 full_process_diagnostics='zero unreviewed backend/validation warnings/errors through teardown; exact loader implicit-disable notices counted separately',
                 limits=['Fixed8-frame authored scene on one GPU;8SPP color is an artifact sanity check, not temporal image quality',
                         'Python independently checks frozen rays, returned-hit geometry/normal/material/depth, previous material-point motion and masks',
                         'C++ immutableWorld separately checks nearest-hit and true misses; binary Python checker does not duplicate that complete geometry oracle',
                         'No SDK evaluation, DLSS quality/performance, RR, temporal accumulation or cross-device claim'])
    (out/'reconstruction-guides-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(status='passed',harness_checks=len(cases),candidate_frames=8,candidate_pixels=20304,actual_dispatched_no_op_controls=1)))


if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError) as e:
        print(str(e),file=sys.stderr);raise SystemExit(1)
