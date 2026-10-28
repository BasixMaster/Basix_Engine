"""Bounded actual SR validation and expected-failure checks; no SDK download.
Full SDK logs stay local. The receipt contains only selected correctness facts.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--release-exe',type=Path);p.add_argument('--runtime',type=Path,required=True);p.add_argument('--validation-layer-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--check-known-driver-bug',action='store_true');a=p.parse_args()
 exe=a.exe.resolve();runtime=a.runtime.resolve();layer=a.validation_layer_dir.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 lock=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']
 for name,expected in lock['Streamline-SR']['runtime_sha256'].items():
  if hashlib.sha256((runtime/name).read_bytes()).hexdigest()!=expected:raise ValueError('SDK runtime hash mismatch: '+name)
 if hashlib.sha256((layer/'VkLayer_khronos_validation.dll').read_bytes()).hexdigest()!=lock['Vulkan-ValidationLayers']['dll_sha256']:raise ValueError('Validation layer hash mismatch')
 env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1');records=[]
 def run(name,flags=(),expected_text='',custom_runtime=None,release=False,trace=False,allow_pass=False):
  directory=out/name;directory.mkdir(exist_ok=True)
  command=[str(a.release_exe.resolve() if release else exe),'--runtime',str(custom_runtime if custom_runtime is not None else runtime),'--validate','--output',str(directory),*flags]
  local=dict(env)
  if trace:local['VK_LOADER_DEBUG']='layer'
  r=subprocess.run(command,env=local,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=120,cwd=directory)
  log=r.stdout;(directory/'process.log').write_text(log,encoding='utf-8')
  if re.search(r'Basix validation:|NoGraphicsAPI validation:|Validation Error|SYNC-HAZARD',log):raise ValueError(name+': Vulkan validation diagnostics; inspect local log')
  if '[Streamline:2]' in log:raise ValueError(name+': SDK error; inspect local log')
  sdk_warnings=[line for line in log.splitlines() if '[Streamline:1]' in line]
  known=re.compile(r'Hook sl\.common:Vulkan:(CmdBindPipeline|CmdBindDescriptorSets|BeginCommandBuffer) is NOT supported')
  if any(not known.search(line) for line in sdk_warnings):raise ValueError(name+': unreviewed SDK warning; inspect local log')
  if trace and not re.search(r'Insert instance layer[^\n]*VK_LAYER_KHRONOS_validation',log):raise ValueError('Loader trace did not confirm layer insertion')
  if expected_text and not(allow_pass and r.returncode==0) and (r.returncode==0 or expected_text not in log):raise ValueError(name+': expected failure absent')
  if not expected_text and r.returncode!=0:raise ValueError(name+': positive test failed; inspect local log')
  records.append(dict(name=name,status='passed' if not expected_text else 'expected failure observed' if r.returncode else 'known bug not reproduced',exit_code=r.returncode,reviewed_manual_hook_warnings=len(sdk_warnings)))
  return directory,r.returncode
 positive,_=run('positive',trace=True);result=json.loads((positive/'result.json').read_text(encoding='utf-8'))
 if result.get('frames')!=3 or result.get('rgb_component_checks')!=2073600 or not result.get('validation_enabled') or result.get('validation_errors') or result.get('validation_warnings'):raise ValueError('Positive GPU/validation coverage incomplete')
 if not(result.get('rgb_rmse',1)<.03 and result.get('max_error',1)<.15):raise ValueError('Pixel oracle absent or outside acceptance')
 if result.get('input_component_checks')!=922320 or result.get('native_engine_component_comparisons')!=2073600:raise ValueError('Independent input/native comparisons incomplete')
 if not all(result.get(k) for k in ('feature_free_checked','sdk_shutdown_checked','legacy_handoff')):raise ValueError('Required SDK lifetime/state contract absent')
 run('omitted-evaluation',('--omit-evaluation',),'DLSS output nonfinite/sentinel/range failure')
 run('post-evaluation-cleanup',('--fail-after-evaluation',),'injected SDK evaluation cleanup failure')
 run('unsupported-extension',('--invalid-extension',),'NoGraphicsAPI SDK-capable device creation failed')
 run('relative-runtime',expected_text='runtime directory must be absolute',custom_runtime='relative-runtime')
 forged=out/'forged-runtime';forged.mkdir(exist_ok=True);(forged/'sl.interposer.dll').write_bytes(b'Basix deliberately invalid signature fixture')
 run('invalid-signature',expected_text='signature verification failed',custom_runtime=forged)
 if a.release_exe:run('release-refuses-validation',expected_text='--validate requires a Debug build',release=True)
 if a.check_known_driver_bug:run('without-legacy-handoff',('--skip-legacy-handoff',),'DLSS constant-radiance oracle tolerance failed',allow_pass=True)
 receipt=dict(checked_on='2026-10-28',status='passed',cases=records,gpu=result,runtime_version=lock['Streamline-SR']['version'],runtime_sha256=lock['Streamline-SR']['runtime_sha256'],loader_confirmed_validation=True,synchronization_validation_requested=True,validation_dll_sha256=lock['Vulkan-ValidationLayers']['dll_sha256'],whole_process_vulkan_diagnostics='zero warnings/errors/sync-hazards',sdk_warning_scope='Three reviewed manual-hook state-tracking warnings per SDK/device process; no SDK errors or unreviewed warnings.',compatibility_issue='https://github.com/NVIDIA-RTX/Streamline/issues/109',sdk_network_scope='OTA flags disabled; SDK still invokes NGX bootstrap updater. This is not an offline-runtime claim.',limits=['One Windows RTX4090/NVIDIA617.42 configuration','Static constant radiance, zero motion/jitter; not temporal quality or image-detail validation','No RR, FG, SDK resize or actual device-loss recovery','Not a performance benchmark or proof of shipping redistribution compliance'])
 (out/'dlss-results.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print(json.dumps({'status':'passed','cases':len(records),'actual_SR_frames':result['frames'],'RGB_components':result['rgb_component_checks'],'RGB_RMSE':result['rgb_rmse']}))
if __name__=='__main__':
 try:main()
 except (ValueError,OSError,subprocess.SubprocessError) as e:print(str(e),file=sys.stderr);raise SystemExit(1)
