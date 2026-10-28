"""Bounded, serial Vulkan PT acceptance; full process logs remain local.

Uses the pinned validation package and process-only environment. A passing
scene harness does not resolve the separately tracked grazing BSDF limitation.
"""
from pathlib import Path
import argparse,hashlib,json,math,os,re,struct,subprocess,sys

ROOT=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--exe',type=Path,required=True)
 p.add_argument('--shader',type=Path,required=True)
 p.add_argument('--validation-layer-dir',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True)
 a=p.parse_args();exe=a.exe.resolve();shader=a.shader.resolve();layer=a.validation_layer_dir.resolve();out=a.output.resolve()
 out.mkdir(parents=True,exist_ok=True)
 lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['Vulkan-ValidationLayers']
 dll=layer/'VkLayer_khronos_validation.dll'
 if not exe.is_file() or not shader.is_file() or shader.name!='progressive_pt.spv' or hashlib.sha256(dll.read_bytes()).hexdigest()!=lock['dll_sha256']:
  raise ValueError('Executable missing or validation package differs from pin')
 exe_sha=hashlib.sha256(exe.read_bytes()).hexdigest();shader_sha=hashlib.sha256(shader.read_bytes()).hexdigest()
 env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1')
 records=[];results={}
 def run(name,args,positive=True,diagnostic='',trace=False):
  local=dict(env)
  if trace:local['VK_LOADER_DEBUG']='layer'
  else:local.pop('VK_LOADER_DEBUG',None)
  target=out/name
  if target.exists():raise ValueError('Use a fresh output directory: '+name)
  command=[str(exe),'--validate','--shader-dir',str(shader.parent),'--output',str(target),*args]
  process=subprocess.run(command,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=180)
  log=process.stdout;(out/(name+'.log')).write_text(log,encoding='utf-8')
  if (process.returncode==0)!=positive:raise ValueError(name+': unexpected exit; inspect local log')
  if diagnostic and diagnostic not in log:raise ValueError(name+': missing expected rejection')
  if re.search(r'NoGraphicsAPI validation:|Basix validation:|NoGraphicsAPI:|Validation Error|SYNC-HAZARD',log):raise ValueError(name+': process validation/backend diagnostic')
  if trace and not re.search(r'Insert instance layer[^\n]*VK_LAYER_KHRONOS_validation',log):raise ValueError('Validation layer insertion not confirmed')
  if positive:
   result=json.loads((target/'result.json').read_text(encoding='utf-8'))
   expected=dict(width=32,height=32,spp=1024,batch=64,seed=20261028,max_depth=8,random_bits=24,next_event_estimation=True,multiple_importance_sampling=True,russian_roulette=True,split_unequal_area_emitter=True)
   value_flags={'--width':'width','--height':'height','--spp':'spp','--batch':'batch','--seed':'seed','--depth':'max_depth'}
   bool_flags={'--no-nee':'next_event_estimation','--no-mis':'multiple_importance_sampling','--no-rr':'russian_roulette','--unsplit-light':'split_unequal_area_emitter'}
   for index,arg in enumerate(args):
    if arg in value_flags:expected[value_flags[arg]]=int(args[index+1])
    if arg in bool_flags:expected[bool_flags[arg]]=False
   if any(result.get(key)!=value for key,value in expected.items()):raise ValueError(name+': receipt settings differ from requested variant')
   if result['status']!='passed' or not result['validation_enabled'] or result['validation_errors'] or result['validation_warnings']:raise ValueError(name+': validation gate missing')
   if not result['batch_identity'] or not result['reset_identity'] or result['primary_rays']!=result['width']*result['height']:raise ValueError(name+': incomplete primary/progressive checks')
   if not result['cpu_comparison'] or result['random_bits']!=24:raise ValueError(name+': CPU stream comparison missing')
   for key,limit in [('normalized_rmse',.05),('low_radiance_normalized_rmse',.1),('max_channel_mean_relative_error',.02)]:
    if not math.isfinite(result[key]) or result[key]>limit:raise ValueError(name+': independent receipt metric gate failed')
   with (target/'linear.pfm').open('rb') as f:
    if f.readline()!=b'PF\n' or tuple(map(int,f.readline().split()))!=(result['width'],result['height']) or f.readline()!=b'-1.0\n':raise ValueError(name+': invalid PFM header')
    payload=f.read()
   if len(payload)!=result['width']*result['height']*12:raise ValueError(name+': PFM shape mismatch')
   values=struct.unpack('<'+str(len(payload)//4)+'f',payload)
   if not all(math.isfinite(x) and x>=0 for x in values) or max(values)==min(values):raise ValueError(name+': invalid/constant HDR output')
   result['independently_decoded_hdr_components']=len(values);results[name]=result
  records.append(dict(name=name,status='passed',exit_code=process.returncode,scope='scoped scene correctness' if positive else 'expected rejection'))
 run('main',['--width','32','--height','32','--spp','1024'],trace=True)
 small=['--width','16','--height','16','--spp','256']
 run('second-seed',[*small,'--seed','20261029'])
 run('one-scatter',[*small,'--depth','1'])
 run('nee-no-mis',[*small,'--no-mis'])
 run('pure-bsdf',[*small,'--no-nee'])
 run('no-roulette',[*small,'--no-rr'])
 run('unsplit-emitter',[*small,'--unsplit-light'])
 run('omitted-pt',[*small,'--omit-dispatch'],False,'PT output is nonfinite, negative, untouched, or wrong sample count')
 run('bad-epoch',[*small,'--bad-epoch'],False,'PT output is nonfinite, negative, untouched, or wrong sample count')
 run('invalid-width',['--width','0'],False,'Invalid bounded positive integer')
 missing=out/'missing-shaders';missing.mkdir()
 run('missing-shader-cleanup',[*small,'--shader-dir',str(missing)],False,'shader file not found')
 if hashlib.sha256(exe.read_bytes()).hexdigest()!=exe_sha or hashlib.sha256(shader.read_bytes()).hexdigest()!=shader_sha:raise ValueError('Executable/shader changed during validation')
 receipt=dict(checked_on='2026-10-28',status='passed',scope='Fixed unit-scale triangle scene PT, ordinary Vulkan AS/ray query on NoGraphicsAPI-owned device; separate grazing material gate remains partial',cases=records,results=results,executable_sha256=exe_sha,shader_sha256=shader_sha,requested_case_settings_verified=True,validation_dll_sha256=lock['dll_sha256'],loader_confirmed_validation=True,synchronization_validation_requested=True,full_process_diagnostics='zero unreviewed Vulkan/backend warnings/errors, including startup and teardown',limits=['One Windows RTX4090/NVIDIA617.42 configuration','CPU24 matched streams and float GPU; no independent GPU distribution/energy acceptance','Grazing same-variates BSDF transport discrepancy remains open','Unit-scale float ray offsets/endpoints; thin gaps/large coordinates unverified','One identity TLAS instance and fixed original geometry; no import/textures/motion/DLSS integration','No advanced extension execution or performance comparison'])
 (out/'progressive-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(dict(status='passed',cases=len(records),primary_rays=sum(r['primary_rays'] for r in results.values()),worst_normalized_rmse=max(r['normalized_rmse'] for r in results.values()),worst_low_radiance_rmse=max(r['low_radiance_normalized_rmse'] for r in results.values()))))

if __name__=='__main__':
 try:main()
 except (ValueError,OSError,subprocess.SubprocessError) as error:print(str(error),file=sys.stderr);raise SystemExit(1)
