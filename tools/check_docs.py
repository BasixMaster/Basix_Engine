"""Portable documentation integrity checks; does not test rendering claims."""
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlsplit,unquote
import json,sqlite3,re,datetime
ROOT=Path(__file__).resolve().parents[1]
class Parser(HTMLParser):
 def __init__(self):super().__init__();self.ids=[];self.links=[];self.scripts=[];self.sections=0
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if 'id' in a:self.ids.append(a['id'])
  if tag=='a' and 'href' in a:self.links.append(a['href'])
  if tag in ['script','link','img']:
   u=a.get('src',a.get('href',''))
   if u:self.scripts.append(u)
  if tag=='section':self.sections+=1
def main():
 p=ROOT/'docs/index.html'; text=p.read_text(encoding='utf-8'); text=re.sub(r'<!-- private-cost-meter:start -->.*?<!-- private-cost-meter:end -->','',text,flags=re.S); parsed=Parser();parsed.feed(text)
 errors=[]
 for name in ('README.md','IMPLEMENTATION.md'):
  markdown=(ROOT/name).read_text(encoding='utf-8')
  if re.search(r'\u00c3|\u00e2\u20ac|\ufffd',markdown):errors.append('Markdown encoding damage: '+name)

 if len(parsed.ids)!=len(set(parsed.ids)):errors.append('duplicate IDs')
 for link in parsed.links+parsed.scripts:
  u=urlsplit(link)
  if u.scheme or u.netloc:continue
  target=(p.parent/unquote(u.path)).resolve() if u.path else p
  if not target.is_relative_to(ROOT):errors.append('path escape: '+link)
  if not target.exists():errors.append('missing target: '+link)
  if u.fragment and not u.path and unquote(u.fragment) not in parsed.ids:errors.append('missing anchor: '+link)
 data=json.loads((p.parent/'evidence/library.json').read_text(encoding='utf-8'))
 from roadmap import load as load_roadmap
 if load_roadmap(check_paths=True)!=json.loads((p.parent/'roadmap-status.json').read_text(encoding='utf-8')):errors.append('roadmap projection mismatch')
 ids=[r['id'] for k in ['questions','sources','findings','actions'] for r in data[k]]
 if data!=json.loads((p.parent/'library.json').read_text(encoding='utf-8')):errors.append('root/nested evidence projection mismatch')
 for kind in ['questions','sources','findings','actions']:
  for record in data[kind]:
   if record.get('presentation_anchor') not in parsed.ids:errors.append('missing presentation anchor: '+record['id'])
 if len(ids)!=len(set(ids)):errors.append('duplicate record IDs')
 for f in data['findings']:
  for e in f['evidence']:
   if e['source_id'] not in ids:errors.append('unknown evidence source')
 source_ids={r['id'] for r in data['sources']}
 if not source_ids.issubset(set(parsed.ids)):errors.append('source coverage missing from main page')
 with sqlite3.connect(p.parent/'evidence/index.sqlite') as db:
  count=db.execute('select count(*) from records').fetchone()[0]
  if count!=len(ids):errors.append('FTS coverage mismatch')
  if not db.execute("select count(*) from records where records match 'PETAFAB'").fetchone()[0]:errors.append('PETAFAB FTS search failed')
 for token in ['\ufffd','TODO','__LIBRARY_DATA__']:
  if token in text:errors.append('unexpected token '+token)
 if parsed.sections!=len(__import__('build_docs').sections):errors.append('chapter coverage mismatch')
 result=dict(checked_on=datetime.datetime.now(datetime.timezone.utc).date().isoformat(),status='failed' if errors else 'passed',chapters=parsed.sections,sources=len(source_ids),evidence_records=count,checks=['local targets','unique IDs','anchors','source coverage','FTS coverage and PETAFAB query','encoding tokens','chapter coverage'],errors=errors,not_tested=['GPU execution','SDK compatibility','engine image quality','performance','legal shipping clearance'])
 (p.parent/'verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
 print(json.dumps(result))
 if errors:raise SystemExit(1)
if __name__=='__main__':main()
