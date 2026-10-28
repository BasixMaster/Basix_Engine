"""Acceptance for a bounded static TLAS transform catalog in the actual PT scene."""
# SPDX-License-Identifier: MIT
from pathlib import Path
import argparse,json,os,subprocess,math,struct
from run_visible_validation import scan,sha
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('exe','shader','validation-layer-dir','output'):p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();exe=a.exe.resolve();shader=a.shader.resolve();layer=a.validation_layer_dir.resolve();out=a.output.resolve()
 if out.exists() and any(out.iterdir()):raise ValueError('Use fresh output')
 out.mkdir(parents=True,exist_ok=True);dll=layer/'VkLayer_khronos_validation.dll'
 lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['Vulkan-ValidationLayers']
 if sha(dll)!=lock['dll_sha256'] or shader.name!='progressive_pt.spv':raise ValueError('Pinned shader/layer differs')
 files={'executable':exe,'shader':shader,'validation_dll':dll,'harness':Path(__file__).resolve(),'scanner':ROOT/'tools/run_visible_validation.py','host':ROOT/'src/progressive_gpu.cpp','instance_fixture':ROOT/'src/progressive_boundary_tests.inl'};before={k:sha(v) for k,v in files.items()}
 env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1');env.pop('VK_LOADER_DEBUG',None)
 results={};records=[]
 def run(name,arguments,positive=True,diagnostic='',trace=False):
  local=dict(env)
  if trace:local['VK_LOADER_DEBUG']='layer'
  target=out/name;command=[str(exe),'--validate','--bounded-rays','--width','16','--height','16','--spp','256','--shader-dir',str(shader.parent),'--output',str(target),*arguments]
  process=subprocess.run(command,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=180);(out/(name+'.log')).write_text(process.stdout,encoding='utf-8');reviewed=scan(process.stdout,name,trace)
  if process.returncode!=(0 if positive else 1) or (diagnostic and diagnostic not in process.stdout):raise ValueError(name+': requested outcome not observed')
  if positive:
   r=json.loads((target/'result.json').read_text(encoding='utf-8'));tid=int(arguments[arguments.index('--instance-transform')+1]);offset=float(arguments[arguments.index('--world-offset')+1]) if '--world-offset' in arguments else 0
   expected=dict(status='passed',width=16,height=16,spp=256,batch=64,seed=20261028,max_depth=8,random_bits=24,bounded_rays=True,instance_transform=tid,mirrored_winding_compensated=tid==4,world_offset=offset,camera_relative_rebase=True,validation_enabled=True,validation_errors=0,validation_warnings=0,cpu_comparison=True,batch_identity=True,reset_identity=True,primary_rays=256,sampler='vndf_sampled_state_v1',next_event_estimation=True,multiple_importance_sampling=True,russian_roulette=True,split_unequal_area_emitter=True)
   if any(r.get(k)!=v for k,v in expected.items()):raise ValueError(name+': settings/identity/validation differ')
   for key,limit in [('normalized_rmse',.05),('low_radiance_normalized_rmse',.1),('max_channel_mean_relative_error',.02)]:
    if not isinstance(r[key],(int,float)) or not math.isfinite(r[key]) or not 0<=r[key]<=limit:raise ValueError(name+': CPU quality gate failed')
   data=(target/'linear.pfm').read_bytes();header=b'PF\n16 16\n-1.0\n'
   if not data.startswith(header) or len(data)!=len(header)+768*4:raise ValueError('HDR dimensions differ')
   values=struct.unpack('<768f',data[len(header):])
   if not all(math.isfinite(x) and x>=0 for x in values) or min(values)==max(values):raise ValueError('Invalid/constant HDR')
   r['independently_decoded_hdr_components']=768;results[name]=r
  records.append(dict(name=name,status='passed',exit_code=process.returncode,scope='scoped actual transformed PT scene' if positive else 'expected rejection',reviewed_loader_implicit_disable_messages=reviewed))
 for tid in range(6):run('transform-'+str(tid),['--instance-transform',str(tid)],trace=tid==0)
 run('nonuniform-far',['--instance-transform','2','--world-offset','16777216'])
 run('shear-far',['--instance-transform','3','--world-offset','-16777216'])
 run('invalid-id',['--instance-transform','6'],False,'Invalid instance transform catalog ID')
 run('malformed-id',['--instance-transform','1junk'],False,'Invalid instance transform catalog ID')
 run('missing-mirror-compensation',['--instance-transform','4','--bad-mirror-winding'],False,'Mirrored authored surface orientation failed')
 run('unrebased-moved-instance',['--instance-transform','3','--world-offset','1','--unrebased'],False,'Unrebased translated nonidentity rendering is outside the bounded fixture scope')
 if before!={k:sha(v) for k,v in files.items()}:raise ValueError('Frozen input changed')
 receipt=dict(checked_on='2026-10-28',status='passed',cases=records,results=results,input_sha256=before,input_hashes_unchanged=True,requested_settings_verified=True,loader_confirmed_validation=True,full_process_validation_clean=True,limits=['Six fixed static transforms and one TLAS instance; no general instance-ID remapping or dynamic updates','Empirical RTX4090 ray-spawn allowance; no arbitrary-transform error proof or cross-device guarantee','Reflected rendering reverses object triangle winding before TLAS/world metadata creation to preserve authored outside/emitting sides','Separate CPU world-space scene oracle and bounded16x16/256SPP fixtures, not general image quality or performance acceptance'])
 (out/'instance-progressive-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps(dict(status='passed',cases=len(records),cpu_image_variants=len(results),worst_normalized_rmse=max(r['normalized_rmse'] for r in results.values()))))
if __name__=='__main__':
 try:main()
 except (ValueError,KeyError,OSError,subprocess.SubprocessError) as e:print(str(e));raise SystemExit(1)
