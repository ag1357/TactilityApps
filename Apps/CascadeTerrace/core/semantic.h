#ifndef CASCADE_SEMANTIC_H
#define CASCADE_SEMANTIC_H
#include <stdint.h>
#include <stddef.h>
/* Only authorized records enter this interface. No Game or WORLD_TRUTH type. */
#define CG_FACTS 64
#define CG_ENTITIES 32
#define CG_TEXT 96
#define CG_STEPS 64
#define CG_OUT 8
#define CG_BUCKETS 1536
#define CG_WIDTH 64
#define CG_FEATURES 160
#define CG_HEADS 6
#define CG_OUTPUTS 46

enum CgStatus { CG_UNKNOWN, CG_KNOWLEDGE, CG_BELIEF, CG_RUMOR, CG_CLAIM, CG_MEMORY };
enum CgOp { CG_QUERY, CG_RECALL, CG_COMPARE, CG_CORROBORATE, CG_CONTRADICT,
 CG_CAUSE, CG_CONSEQUENCE, CG_COUNTERFACTUAL, CG_UPDATE_BELIEF, CG_CHECK_SOURCE,
 CG_CHECK_KNOWLEDGE, CG_ABSTAIN, CG_CLARIFY, CG_OPS };
enum CgRelation { CG_IDENTITY, CG_OCCUPATION, CG_RELATIONSHIP, CG_LOCATION,
 CG_CONDITION, CG_POSSESSION, CG_GOAL, CG_RESPONSIBILITY, CG_TRUST, CG_PROMISE,
 CG_EVENT, CG_OBSERVATION, CG_BALANCE, CG_SCHEDULE, CG_STRENGTH, CG_COST,
 CG_REQUIREMENT, CG_AGE, CG_OTHER, CG_RELATIONS };
enum CgAct { CG_QUESTION, CG_ASSERTION, CG_COMMITMENT };
enum CgFlags { CG_RULE=1, CG_EXCLUSIVE=2, CG_NEGATED=4, CG_BINARY=8 };
enum CgDisposition { CG_ANSWER, CG_NO_KNOWLEDGE, CG_NEEDS_CLARIFICATION, CG_REJECTED, CG_LIMIT };
typedef struct { uint32_t id; char name[48]; } CgEntity;
typedef struct {
 uint32_t id, subject, source; int32_t value; int64_t time;
 uint32_t deps[2]; uint8_t relation, status, flags, dep_count;
 char text[CG_TEXT];
} CgFact;
typedef struct {
 CgFact facts[CG_FACTS]; CgEntity entities[CG_ENTITIES];
 uint16_t count, entity_count; uint32_t self, player;
} CgView;
typedef struct { uint32_t focus, last_fact; uint8_t relation, valid; } CgContext;
typedef struct {
 uint32_t entity, second_entity, intervention; int32_t set_value;
 uint8_t op, relation, second_relation, epistemic, act, negation, hypothetical;
 uint8_t ambiguous, unresolved, temporal, confidence;
} CgFrame;
typedef struct {
 uint32_t fact, parents[2]; int32_t value;
 uint8_t op, status, hypothetical, legal;
} CgStep;
typedef struct {
 CgFrame frame; CgStep steps[CG_STEPS]; uint32_t consulted[CG_FACTS], outputs[CG_OUT];
 int32_t values[CG_OUT]; uint8_t statuses[CG_OUT];
 uint16_t step_count, consulted_count; uint8_t count, disposition, conflict, verified;
 uint32_t inference_ops; char text[1024];
} CgResult;
extern const char* const cg_relations[CG_RELATIONS];
extern const char* const cg_operators[CG_OPS];
extern const uint8_t cg_head_sizes[CG_HEADS];
void cg_parse(const CgView*, const CgContext*, const char*, int learned, CgFrame*);
void cg_execute(const CgView*, const CgFrame*, CgResult*);
void cg_respond(const CgView*, CgContext*, const char*, int learned, CgResult*);
uint32_t cg_legal_mask(const CgView*);
int cg_verify(const CgView*, const CgResult*);
void cg_realize(const CgView*, CgResult*);
int cg_features(const CgView*,const char*,uint16_t*,size_t);
void cg_predict(const uint16_t*,int,uint8_t*,uint8_t*);
uint32_t cg_hash(const char*);
int cg_fact_index(const CgView*,uint32_t);
#endif
