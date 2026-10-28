"""Roadmap truthfulness and dependency regressions. Standard library only."""
import json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import roadmap
class RoadmapTests(unittest.TestCase):
 def setUp(self):self.data=json.loads((ROOT/'roadmap.json').read_text(encoding='utf-8'))
 def rejected(self,data):
  work=ROOT/'work';work.mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(dir=work,prefix='roadmap-test-') as tmp:
   path=Path(tmp)/'roadmap.json';path.write_text(json.dumps(data),encoding='utf-8')
   with self.assertRaises(ValueError):roadmap.load(path)
 def test_current_evidence_exists(self):roadmap.load(check_paths=True)
 def test_unknown_status(self):
  self.data['items'][0]['status']='nearly_done';self.rejected(self.data)
 def test_completed_requires_evidence(self):
  self.data['items'][0]['evidence']=[];self.rejected(self.data)
 def test_parent_cannot_complete_over_pending_children(self):
  next(x for x in self.data['items'] if x['id']=='M1b')['status']='completed'
  self.rejected(self.data)
 def test_actual_blocker_required(self):
  x=next(x for x in self.data['items'] if x['id']=='M1b-sdk');x['status']='blocked';x.pop('blocker',None);self.rejected(self.data)
 def test_dependency_cycle_rejected(self):
  self.data['items'][0]['prerequisites']=['M0b'];self.rejected(self.data)
 def test_evidence_cannot_escape_project(self):
  self.data['items'][0]['evidence'][0]['path']='../private-data.json';self.rejected(self.data)
if __name__=='__main__':unittest.main()
