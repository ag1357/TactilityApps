#!/usr/bin/env python3
import json,statistics
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out={'physical_status':'PENDING PHYSICAL HARDWARE VALIDATION','overall_Q1':'FAIL / INCOMPLETE', 'core':json.loads((root/'results/core-tests.json').read_text()),'dialogue':{}, 'cautions':['Fact-hit scoring is a lower-bound retrieval check, not the full required-behavior judgment.','Provenance coverage does not measure relevance, completeness, fluency or correctness of inferences.','Paraphrases were written by the implementation author, not an independent blinded evaluator.']}
for mode in [0,1]:
 rows=[json.loads(s) for p in (root/'results').glob(f'dialogue-{mode}-*.jsonl') for s in p.read_text().splitlines()]
 held=[json.loads(s) for p in (root/'results').glob(f'paraphrases-{mode}-*.jsonl') for s in p.read_text().splitlines()]
 latency=sorted(x['latency_ms'] for x in rows)
 out['dialogue']['template' if mode==0 else 'composition']={'turns':len(rows),'minimum_behavior_hits':sum(x['minimum_behavior_pass'] for x in rows),'provenance_valid_turns':sum(x['provenance_valid'] for x in rows),'median_ms':statistics.median(latency),'p95_ms':latency[int(.95*(len(latency)-1))],'paraphrase_turns':len(held),'paraphrase_hits':sum(x['minimum_behavior_pass'] for x in held),'neural_model_bytes':0,'streaming':'not implemented; first output equals complete response','same_retrieval':True}
npc=out['core']['npc_bytes'];out['npc_storage_projection']={str(n):n*npc for n in [1,100,1000,10000]}
(root/'results/summary.json').write_text(json.dumps(out,indent=2)+'\n')
print(json.dumps(out,indent=2))
