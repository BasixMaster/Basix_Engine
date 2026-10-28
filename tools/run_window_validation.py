"""Run the owned hidden-window executable with bounded lifetime and whole-process logs.
No downloads or machine-wide driver/layer changes. A supplied layer directory is
used only in each child environment; the caller must verify its provenance.
"""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--layer-dir',type=Path)
    p.add_argument('--timeout-seconds',type=int,default=45)
    a=p.parse_args()
    if not 1<=a.timeout_seconds<=120:p.error('timeout must be from 1 to 120 seconds')
    exe=a.executable.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if not exe.is_file():p.error('executable does not exist')
    env=os.environ.copy();layer_files={}
    env['VK_LOADER_LAYERS_DISABLE']='~implicit~'
    if a.layer_dir:
        layer=a.layer_dir.resolve()
        for name in ('VkLayer_khronos_validation.json','VkLayer_khronos_validation.dll'):
            f=layer/name
            if not f.is_file():p.error('missing validation layer component: '+name)
            layer_files[name]=digest(f)
        lock=json.loads((Path(__file__).resolve().parents[1]/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['Vulkan-ValidationLayers']
        if layer_files['VkLayer_khronos_validation.dll']!=lock['dll_sha256']:p.error('Validation DLL differs from the explicitly checked pin')
        env['VK_LAYER_PATH']=str(layer)
        env.pop('VK_LAYER_ENABLES',None)
        env.pop('VK_LAYER_VALIDATE_SYNC',None)
        env['VK_VALIDATION_VALIDATE_SYNC']='1'
    runs=[];errors=[]
    for case in ('normal','before-device','after-device','after-present'):
        directory=out/case;directory.mkdir(exist_ok=True)
        command=[str(exe),'--output',str(directory)]
        if a.layer_dir:command+=['--validate']
        if case!='normal':command+=['--fail-at',case]
        expected=0 if case=='normal' else 2
        try:
            run=subprocess.run(command,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=a.timeout_seconds)
            (directory/'stdout.log').write_text(run.stdout,encoding='utf-8')
            (directory/'stderr.log').write_text(run.stderr,encoding='utf-8')
            diagnostics=('NoGraphicsAPI validation:' in run.stderr or 'Basix validation:' in run.stderr or 'Validation Error' in run.stderr or 'Validation Warning' in run.stderr)
            if run.returncode!=expected:errors.append(case+': unexpected exit '+str(run.returncode))
            if diagnostics:errors.append(case+': validation diagnostics emitted')
            receipt_path=directory/'result.json'
            receipt=json.loads(receipt_path.read_text(encoding='utf-8')) if receipt_path.is_file() else {}
            if not receipt.get('window_destroyed') or not receipt.get('window_remained_hidden'):errors.append(case+': missing hidden-window cleanup evidence')
            if a.layer_dir and case!='before-device' and not receipt.get('validation_enabled'):errors.append(case+': required validation unavailable')
            if case=='normal' and (receipt.get('presented_frames')!=8 or receipt.get('empty_acquires')!=3):errors.append(case+': incomplete coverage')
            runs.append(dict(case=case,exit_code=run.returncode,expected_exit_code=expected,validation_diagnostics=diagnostics,receipt=receipt))
        except subprocess.TimeoutExpired as e:
            errors.append(case+': process timeout; owned child terminated, cleanup not established')
            (directory/'timeout.log').write_text(str(e),encoding='utf-8')
            runs.append(dict(case=case,status='timeout'))
    result=dict(status='failed' if errors else 'passed',scope='Hidden Win32 WSI and controlled application-failure cleanup; no real device loss or SDK.',sync_validation_requested=bool(a.layer_dir),layer_sha256=layer_files,timeout_seconds=a.timeout_seconds,runs=runs,errors=errors)
    (out/'window-validation.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(status=result['status'],runs=len(runs),errors=errors)))
    return bool(errors)
if __name__=='__main__':sys.exit(main())
