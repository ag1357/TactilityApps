#!/usr/bin/env python3
"""Machine-readable complete line inventory; broad categories intentionally overlap."""
import re,json,hashlib
from abi import ROOT
p=ROOT/'research/rung_a/cognition.c';s=p.read_text();rules={
'scenario_lexical':r'Kyra|kyra|Oren|oren|Dax|dax|Marisol|marisol|Halden|halden|Guild|guild|station|intake|coupling|cable|phos|consortium|pressure|market|scorch',
'entity_rule':r'E_(OREN|DAX|MARISOL|HALDEN|GUILD|STATION|COUPLING|FIELD|INCIDENT|LOG|CABLE)|KYRA_ID|IT_COUPLING',
'evaluation_sensitive':r'E_UNKNOWABLE|R_SECRET|R_LEADERSHIP|meta =|tok_has.*(really|lying|lie|pretend|ignore)|fr.entity = E_STATION|fr.entity = E_INCIDENT',
'fixed_prose':r'"[^"\n]*[ .:;?!—][^"\n]*"',
'mystery_inference':r'fr.hypothetical.*fr.repair_cue|fr.contrast|fr.entity == E_DAX|fr.repair_cue \|\||R_RESPONSIBILITY.*R_CONDUCT|source_name|fact (14|21|22|23)'}
inv={k:[{'line':i,'text':l} for i,l in enumerate(s.splitlines(),1) if re.search(pattern,l)] for k,pattern in rules.items()}
report={'source_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'line_inventory':inv,'counts':{k:len(v) for k,v in inv.items()},'manual_findings':[
'Entity enum and tags hardcode all mystery people, station, incident, log, cable, field and distant unknowable locations.',
'Fact classification uses subject substrings and relation substrings; brother/sibling always binds Oren.',
'Pronouns default to station or incident rather than resolving arbitrary context.',
'Coupling condition maps to station; unspecified condition/requirement maps to station, age to incident.',
'Source IDs 1/2/100 become own observation/Marisol/player even when ID semantics differ.',
'Every promise becomes a coupling delivery; statements about station/repair are checked by fixed repaired-memory search.',
'Who-responsible selects Dax belief; why-responsible selects Dax location rumor and fixed leaning-toward-Dax prose.',
'Counterfactual branch emits repair/blame independence and conclusions about Oren without performing a graph intervention.',
'Contrast log/Oren handler emits missing-entry and no-exoneration conclusions not justified by the checked IDs alone.',
'Unknowable and adversarial vocabulary is evaluation-sensitive; word pretend rejects ordinary hypotheticals.',
'Greetings contain fixed station goal prose; IDs >=1000 encode memory/claim source conventions.',
'Fact-hit and ID/status membership metrics cannot verify the additional natural-language propositions.',
'Generality probe changes seed/variant/trust, not narrative schema, arbitrary identities, or causal structure.'
], 'disposition':'Experimental Rung A only. No claim of general reasoning or production gate.'}
(ROOT/'research/results/patch-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report['counts']))
