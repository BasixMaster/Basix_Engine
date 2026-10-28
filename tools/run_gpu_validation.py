"""Run bounded, process-local Debug Vulkan/SyncVal and GPU failure-path checks.
Full logs stay in the selected output directory. No system Vulkan setting is changed.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--exe',type=Path,required=True);p.add_argument('--validation-layer-dir',type=Path,required=True)
 p.add_argument('--release-exe',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 exe=a.exe.resolve();layer=a.validation_layer_dir.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['Vulkan-ValidationLayers']
 dll=layer/'VkLayer_khronos_validation.dll';manifest=layer/'VkLayer_khronos_validation.json'
 if not exe.is_file() or not manifest.is_file() or not dll.is_file():raise ValueError('Executable/validation package missing')
 if hashlib.sha256(dll.read_bytes()).hexdigest()!=lock['dll_sha256']:raise ValueError('Validation DLL differs from the checked package; audit/update its explicit pin first.')
 env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1')
 records=[]
 def run(name,args,expected_success,expected_text='',trace=False):
  local=dict(env)
  if trace:local['VK_LOADER_DEBUG']='layer'
  else:local.pop('VK_LOADER_DEBUG',None)
  r=subprocess.run(args,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=90)
  log=r.stdout;(out/(name+'.log')).write_text(log,encoding='utf-8')
  if (r.returncode==0)!=expected_success:raise ValueError(name+': unexpected exit code; inspect local log')
  if expected_text and expected_text not in log:raise ValueError(name+': expected failure diagnostic absent')
  if re.search(r'NoGraphicsAPI validation:|Basix validation:|Validation Error|SYNC-HAZARD',log):raise ValueError(name+': validation diagnostic emitted; inspect local log')
  if trace and not re.search(r'Insert instance layer[^\n]*VK_LAYER_KHRONOS_validation',log):raise ValueError('Loader trace did not confirm insertion of the validation layer')
  records.append(dict(name=name,status='passed',exit_code=r.returncode,scope='positive GPU check' if expected_success else 'expected failure; cleanup path'))
 positive=out/'positive';positive.mkdir(exist_ok=True)
 run('core-sync-positive',[str(exe),'--validate','--cycles','3','--output',str(positive)],True,trace=True)
 result=json.loads((positive/'result.json').read_text(encoding='utf-8'))
 if not result['validation_enabled'] or result['validation_errors'] or result['validation_warnings']:raise ValueError('Positive receipt lacks clean enabled validation')
 if (result['pointer_pixels'],result['ray_checks'],result['texture_pixel_checks'])!=(12288,24576,25500):raise ValueError('Oracle coverage differs from expected three-cycle test')
 if result.get('contract_rejections')!=63:raise ValueError('Borrow/retirement rejection coverage incomplete')
 run('invalid-cycle',[str(exe),'--cycles','0'],False,'--cycles must be')
 missing=out/'missing-shaders';missing.mkdir(exist_ok=True)
 run('missing-shader-cleanup',[str(exe),'--validate','--shader-dir',str(missing),'--output',str(out/'shader-failure')],False,'shader file not found')
 blocked=out/'image-failure';blocked.mkdir(exist_ok=True);(blocked/'pointer-smoke.ppm').mkdir(exist_ok=True)
 run('image-write-cleanup',[str(exe),'--validate','--output',str(blocked)],False,'image write failed')
 if a.release_exe:run('release-refuses-validation',[str(a.release_exe.resolve()),'--validate','--output',str(out/'release-rejected')],False,'--validate requires a Debug build')
 receipt=dict(checked_on='2026-10-28',status='passed',configuration='Debug',validation_layer='Khronos nightly checked 2026-10-28',validation_manifest_api=json.loads(manifest.read_text(encoding='utf-8'))['layer']['api_version'],validation_dll_sha256=lock['dll_sha256'],loader_confirmed_validation_layer=True,synchronization_validation_requested=True,diagnostic_log_scan='zero warning/error and sync-hazard diagnostics, including startup/shutdown',process_only_environment=True,cases=records,gpu=result,limits=['One RTX4090/Windows configuration','Headless same-queue offscreen extents; not window/swapchain resize','No actual DLSS SDK evaluation','No device-loss recovery or cross-queue ownership test','No independent spirv-val pass or performance benchmark'])
 (out/'validation-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(dict(status='passed',cases=len(records),pointer_pixels=result['pointer_pixels'],ray_checks=result['ray_checks'],texture_pixel_checks=result['texture_pixel_checks'],validation_errors=0,validation_warnings=0)))
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,subprocess.SubprocessError) as e:print(str(e),file=sys.stderr);raise SystemExit(1)
