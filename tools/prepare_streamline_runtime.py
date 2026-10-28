"""Stage a pinned, locally supplied optional SDK runtime. No download or install.
The staged NVIDIA DLLs retain their licenses; they are never copied into Basix.
"""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--sdk-root',type=Path,required=True);p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
 src=a.sdk_root.resolve();dst=a.destination.resolve()
 if dst.exists() or dst==src or dst.is_relative_to(src) or src.is_relative_to(dst):p.error('Use a new separate destination outside the SDK tree')
 if dst.is_relative_to(ROOT):p.error('Runtime binaries must remain outside the distributable Basix source tree')
 pin=json.loads((ROOT/'dependencies.lock.json').read_text(encoding='utf-8'))['dependencies']['Streamline-SR']
 for name,expected in pin['runtime_sha256'].items():
  f=src/'bin/x64'/name
  if not f.is_file() or hashlib.sha256(f.read_bytes()).hexdigest()!=expected:p.error('Runtime component differs from the checked package: '+name)
 notices=['license.txt','3rd-party-licenses.md','bin/x64/nvngx_dlss.license.txt','bin/x64/reflex.license.txt']
 for name in notices:
  if not (src/name).is_file():p.error('Missing SDK notice: '+name)
 dst.mkdir(parents=True)
 for name in pin['runtime_sha256']:shutil.copy2(src/'bin/x64'/name,dst/name)
 for name in notices:shutil.copy2(src/name,dst/Path(name).name)
 (dst/'basix-runtime-manifest.json').write_text(json.dumps({'version':pin['version'],'sha256':pin['runtime_sha256'],'rights':'Optional NVIDIA runtime components; separate terms and notices apply.'},indent=2)+'\n',encoding='utf-8')
 print(json.dumps({'status':'staged and hash-verified','components':len(pin['runtime_sha256']),'notices':len(notices)}))
if __name__=='__main__':main()
