"""Check actual GPU HDR identity after bounded double-to-local fixture translation."""
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
 lock=json.loads((ROOT/'dependencies.lock.json').read_text())['dependencies']['Vulkan-ValidationLayers']
 if sha(dll)!=lock['dll_sha256'] or shader.name!='progressive_pt.spv':raise ValueError('Pinned shader/layer contract differs')
 inputs={'executable':exe,'shader':shader,'validation_dll':dll,'harness':Path(__file__).resolve()};before={k:sha(v) for k,v in inputs.items()};results={};images=[];cases=[]
 for index,offset in enumerate((0,16777216,-16777216)):
  env=dict(os.environ,VK_LAYER_PATH=str(layer),VK_LOADER_LAYERS_DISABLE='~implicit~',VK_VALIDATION_VALIDATE_SYNC='1');env.pop('VK_LOADER_DEBUG',None)
  if index==0:env['VK_LOADER_DEBUG']='layer'
  target=out/('case-'+str(index));process=subprocess.run([str(exe),'--validate','--bounded-rays','--no-cpu-comparison','--width','16','--height','16','--spp','256','--world-offset',str(offset),'--shader-dir',str(shader.parent),'--output',str(target)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',timeout=120)
  (out/('case-'+str(index)+'.log')).write_text(process.stdout,encoding='utf-8');scan(process.stdout,'coordinate case',index==0)
  if process.returncode:raise ValueError('Coordinate render rejected')
  r=json.loads((target/'result.json').read_text());expected=dict(status='passed',width=16,height=16,spp=256,world_offset=offset,camera_relative_rebase=True,bounded_rays=True,validation_enabled=True,validation_errors=0,validation_warnings=0,batch_identity=True,reset_identity=True,cpu_comparison=False)
  if any(r.get(k)!=v for k,v in expected.items()) or any(r[k] is not None for k in ('normalized_rmse','low_radiance_normalized_rmse','max_channel_mean_relative_error')):raise ValueError('Requested coordinate settings or explicit CPU-skip metadata differ')
  data=(target/'linear.pfm').read_bytes();header=b'PF\n16 16\n-1.0\n'
  if not data.startswith(header) or len(data)!=len(header)+16*16*12:raise ValueError('HDR dimensions differ')
  values=struct.unpack('<768f',data[len(header):])
  if not all(math.isfinite(x) and x>=0 for x in values) or min(values)==max(values):raise ValueError('Invalid/constant HDR')
  images.append(data);results[str(offset)]=r;cases.append(dict(world_offset=offset,image_sha256=sha(target/'linear.pfm'),status='passed'))
 if not all(x==images[0] for x in images) or before!={k:sha(v) for k,v in inputs.items()}:raise ValueError('Translation identity or frozen inputs failed')
 receipt=dict(status='passed',cases=cases,results=results,input_sha256=before,input_hashes_unchanged=True,hdr_byte_identity=True,loader_confirmed_validation=True,full_process_validation_clean=True,scope='Three rebased double-precision translations of the fixed fixture,16x16/256SPP; no CPU comparison in these runs. Independent CPU scene acceptance is separate. Does not recover pre-rounded geometry or establish large-world streaming.')
 (out/'ray-coordinate-results.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(dict(status='passed',cases=3,hdr_byte_identity=True)))
if __name__=='__main__':
 try:main()
 except (ValueError,KeyError,OSError,subprocess.SubprocessError) as e:print(str(e));raise SystemExit(1)
