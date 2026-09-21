#!/usr/bin/env python3
"""Seal implementation/model/evaluation before opening final worlds. Refuses overwrite."""
from pathlib import Path
import hashlib,json,subprocess,datetime
from abi import ROOT
out=ROOT/'research/results/freeze.json'
if out.exists():raise SystemExit('Already frozen. No replacement of final selection permitted.')
files=list((ROOT/'core').glob('semantic.*'))+[ROOT/'core/decision.c',ROOT/'core/general_dialogue.c']+list((ROOT/'research').glob('*.py'))+list((ROOT/'research/models').glob('*'))+list((ROOT/'research/data').glob('*'))+list((ROOT/'research/rung_a').glob('*.c'))
record={'starting_commit':'feab7c0c161a0e8b8f3dedef6249a38b8f5b5f24','implementation_checkpoint':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'selection':'one 101294-parameter model, epoch chosen on validation; no scaling; no recurrent/LTC sweep','final_opened':False,'gate':{'exact_answer_min':0.90,'each_required_class_min':0.80,'provenance_min':1.0,'unsupported_propositions_max':0,'truth_leak_max':0,'game_fact_hits_min':120,'learned_must_exceed_B':True},'sha256':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}}
out.write_text(json.dumps(record,indent=2)+'\n');print('Implementation and evaluation frozen. Final partition remains unopened.')
