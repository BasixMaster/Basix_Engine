"""Roadmap truthfulness and dependency regressions. Standard library only."""
import json,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import roadmap
class RoadmapTests(unittest.TestCase):
 def setUp(self):self.data=json.loads((ROOT/'roadmap.json').read_text(encoding='utf-8'))
 def rejected(self,data,check_paths=False):
  work=ROOT/'work';work.mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(dir=work,prefix='roadmap-test-') as tmp:
   path=Path(tmp)/'roadmap.json';path.write_text(json.dumps(data),encoding='utf-8')
   with self.assertRaises(ValueError):roadmap.load(path,check_paths=check_paths)
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
 def test_current_status_must_match_latest_structured_history(self):
  item=self.data['items'][0]
  self.data['history']=[dict(checked_on='2026-10-28',item_id=item['id'],from_status='pending',to_status='partial',event='Only a subset was verified.')]
  self.rejected(self.data)
 def test_broken_history_chain_rejected(self):
  item=self.data['items'][0]
  self.data['history']=[
   dict(checked_on='2026-10-27',item_id=item['id'],from_status='pending',to_status='in_progress',event='Started scoped implementation.'),
   dict(checked_on='2026-10-28',item_id=item['id'],from_status='partial',to_status=item['status'],event='The predecessor state does not agree.')]
  self.rejected(self.data)
 def test_structured_history_is_chronological(self):
  item=self.data['items'][0]
  self.data['history']=[
   dict(checked_on='2026-10-28',item_id=item['id'],from_status='pending',to_status='in_progress',event='Started implementation.'),
   dict(checked_on='2026-10-27',item_id=item['id'],from_status='in_progress',to_status=item['status'],event='Earlier date cannot follow.')]
  self.rejected(self.data)
 def test_invalid_history_evidence_rejected(self):
  self.data['history']=[dict(checked_on='2026-10-28',event='Receipt must stay inside project.',evidence=['../private-data.json'])]
  self.rejected(self.data,check_paths=True)
 def test_missing_history_evidence_rejected_when_checking_paths(self):
  self.data['history']=[dict(checked_on='2026-10-28',event='A missing file is not evidence.',evidence=['docs/never-recorded-history-receipt-034f.json'])]
  self.rejected(self.data,check_paths=True)
 def test_history_unknown_item_or_state_rejected(self):
  for item,state in [('unknown-gate','pending'),(self.data['items'][0]['id'],'nearly_done')]:
   self.data['history']=[dict(checked_on='2026-10-28',item_id=item,from_status='pending',to_status=state,event='Invalid transition metadata.')]
   self.rejected(self.data)
 def test_partial_transition_record_rejected(self):
  self.data['history']=[dict(checked_on='2026-10-28',item_id=self.data['items'][0]['id'],event='A transition cannot omit its states.')]
  self.rejected(self.data)
 def test_legacy_history_preserved_and_rendered(self):
  note='Earlier baseline: partial coverage <still open> | evidence retained.'
  self.data['history']=[dict(checked_on='2026-10-28',event=note)]
  work=ROOT/'work';work.mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(dir=work,prefix='roadmap-history-test-') as tmp:
   path=Path(tmp)/'roadmap.json';path.write_text(json.dumps(self.data),encoding='utf-8')
   loaded=roadmap.load(path)
   self.assertEqual(loaded['history'],self.data['history'])
   page=roadmap.render(path);text=roadmap.markdown(loaded)
   self.assertIn('Milestone and status logbook',page)
   self.assertIn('partial coverage &lt;still open&gt;',page)
   self.assertIn('General / legacy note',page)
   self.assertIn('Earlier baseline:',text)
   self.assertIn('No structured transition recorded',text)
 def test_valid_chain_shows_evidence_and_progress(self):
  item=self.data['items'][0];evidence=item['evidence'][0]['path']
  self.data['history']=[
   dict(checked_on='2026-10-27',item_id=item['id'],from_status='pending',to_status='in_progress',event='Began scoped work.'),
   dict(checked_on='2026-10-28',item_id=item['id'],from_status='in_progress',to_status=item['status'],event='Acceptance passed for the named scope.',evidence=[evidence])]
  work=ROOT/'work';work.mkdir(exist_ok=True)
  with tempfile.TemporaryDirectory(dir=work,prefix='roadmap-history-test-') as tmp:
   path=Path(tmp)/'roadmap.json';path.write_text(json.dumps(self.data),encoding='utf-8')
   loaded=roadmap.load(path,check_paths=True)
   self.assertIn('pending → in progress',roadmap.markdown(loaded))
   self.assertIn('Acceptance passed for the named scope.',roadmap.render(path))
   self.assertIn('../'+evidence,roadmap.render(path))
if __name__=='__main__':unittest.main()
