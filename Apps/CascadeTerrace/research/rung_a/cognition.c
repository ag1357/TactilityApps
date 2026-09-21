#include "game.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Semantic cognition dialogue engine (mode 2).
 *
 * Pipeline (co-agent target): natural language -> semantic frame ->
 * grounded context binding -> cognition/inference -> response proposition ->
 * language realization. Operates only over the NPC-authorized view built by
 * npc_view(); WORLD_TRUTH is never an available record. Zero neural bytes:
 * this is the symbolic first stage of the intended progression. All storage is
 * bounded and integer; no malloc, no floating point in the decision path.
 */

/* ---- Concept vocabulary ---------------------------------------------- */

enum {                /* entities */
    E_NONE = 0,
    E_SELF,           /* Kyra / "you" */
    E_PLAYER,         /* the speaker / "me" */
    E_OREN,
    E_MARISOL,
    E_HALDEN,
    E_DAX,
    E_GUILD,
    E_STATION,
    E_COUPLING,
    E_FIELD,
    E_MARKET,
    E_INCIDENT,
    E_LOG,
    E_CABLE,
    E_PRONOUN,        /* it / that -> resolve from context */
    E_UNKNOWABLE      /* consortium leadership, far geography, France ... */
};

enum {                /* relations */
    R_NONE = 0,
    R_NAME,
    R_OCCUPATION,
    R_MEMBERSHIP,
    R_RELATIONSHIP,
    R_CONDITION,
    R_OUTPUTLOSS,
    R_OBSERVATION,
    R_AGE,
    R_SHIFT,
    R_COST,
    R_PRICE,
    R_REQUIREMENT,
    R_DELAY,
    R_STRENGTH,
    R_LEVEL,
    R_LOCATION,
    R_RESPONSIBILITY,
    R_CONDUCT,
    R_INTENTION,
    R_BALANCE,
    R_TRUST,
    R_SCHEDULE,
    R_GOAL,
    R_PRESSURECHECK,
    R_COMMITMENT,
    R_SAID,
    R_MEMORY,
    R_RUMORREL,       /* explicit ask about rumors */
    R_SECRET,         /* unknowable: private/secret */
    R_LEADERSHIP,     /* unknowable: who runs X */
    R_GEOGRAPHY       /* unknowable: distant places */
};

enum { WH_NONE = 0, WH_WHO, WH_WHAT, WH_WHEN, WH_WHERE, WH_WHY, WH_HOW, WH_HOWMUCH, WH_WHETHER, WH_WHICH };
enum { ACT_EMPTY = 0, ACT_QUESTION, ACT_ASSERTION, ACT_PROMISE, ACT_GREETING, ACT_META };

typedef struct {
    int act;
    int wh;
    int entity;        /* primary resolved entity */
    int relation;      /* resolved relation */
    int has_self, has_player, has_pronoun;
    int belief_cue;    /* think/believe/suspect */
    int rumor_cue;     /* rumor/heard */
    int temporal;      /* remember/earlier/did/told */
    int source_cue;    /* why/how-do-you-know/that */
    int hypothetical;  /* if ... hadn't */
    int contrast;      /* but ... says (reconcile) */
    int negation;
    int repair_cue;    /* fix / repair / restore */
} Frame;

typedef struct {
    const char* word;
    int kind;          /* 1 entity, 2 relation, 3 wh, 4 mark */
    int value;
} Tag;

/* mark values */
enum { M_SELF = 1, M_PLAYER, M_PRONOUN, M_BELIEF, M_RUMOR, M_TEMPORAL, M_SOURCE,
       M_HYP, M_REMOVE, M_CONTRAST, M_NEG, M_GREET, M_META, M_PROMISE, M_REPAIR, M_GIVE };

static const Tag tags[] = {
    /* wh */
    {"who", 3, WH_WHO}, {"whom", 3, WH_WHO}, {"what", 3, WH_WHAT}, {"whats", 3, WH_WHAT},
    {"when", 3, WH_WHEN}, {"where", 3, WH_WHERE}, {"why", 3, WH_WHY}, {"how", 3, WH_HOW},
    {"which", 3, WH_WHICH},
    /* entities */
    {"you", 1, E_SELF}, {"your", 1, E_SELF}, {"yours", 1, E_SELF}, {"youre", 1, E_SELF}, {"kyra", 1, E_SELF},
    {"i", 1, E_PLAYER}, {"me", 1, E_PLAYER}, {"my", 1, E_PLAYER}, {"mine", 1, E_PLAYER},
    {"oren", 1, E_OREN}, {"brother", 1, E_OREN}, {"sibling", 1, E_OREN},
    {"marisol", 1, E_MARISOL}, {"vendor", 1, E_MARISOL},
    {"halden", 1, E_HALDEN}, {"supervisor", 1, E_HALDEN},
    {"dax", 1, E_DAX},
    {"guild", 1, E_GUILD},
    {"station", 1, E_STATION}, {"intake", 1, E_STATION},
    {"coupling", 1, E_COUPLING},
    {"field", 1, E_FIELD}, {"waterfall", 1, E_FIELD}, {"phos", 1, E_FIELD},
    {"market", 1, E_MARKET}, {"chits", 1, E_MARKET},
    {"incident", 1, E_INCIDENT},
    {"log", 1, E_LOG},
    {"cable", 1, E_CABLE},
    {"it", 1, E_PRONOUN}, {"that", 1, E_PRONOUN},
    {"consortium", 1, E_UNKNOWABLE}, {"elek", 1, E_UNKNOWABLE}, {"france", 1, E_UNKNOWABLE},
    {"capital", 1, E_UNKNOWABLE}, {"empire", 1, E_UNKNOWABLE}, {"royal", 1, E_UNKNOWABLE},
    {"ridge", 1, E_UNKNOWABLE}, {"northern", 1, E_UNKNOWABLE}, {"beyond", 1, E_UNKNOWABLE},
    /* relations */
    {"name", 2, R_NAME}, {"identity", 2, R_NAME}, {"called", 2, R_NAME},
    {"job", 2, R_OCCUPATION}, {"work", 2, R_OCCUPATION},
    {"technician", 2, R_OCCUPATION}, {"profession", 2, R_OCCUPATION}, {"occupation", 2, R_OCCUPATION},
    {"membership", 2, R_MEMBERSHIP},
    {"happened", 2, R_CONDITION}, {"happen", 2, R_CONDITION}, {"wrong", 2, R_CONDITION},
    {"broke", 2, R_CONDITION}, {"broken", 2, R_CONDITION}, {"condition", 2, R_CONDITION},
    {"cracked", 2, R_CONDITION}, {"status", 2, R_CONDITION},
    {"long", 2, R_AGE}, {"old", 2, R_AGE}, {"ago", 2, R_AGE},
    {"cost", 2, R_COST}, {"expensive", 2, R_COST}, {"worth", 2, R_COST}, {"much", 2, R_COST}, {"price", 2, R_PRICE},
    {"fix", 2, R_REQUIREMENT}, {"mend", 2, R_REQUIREMENT}, {"restore", 2, R_REQUIREMENT},
    {"take", 2, R_REQUIREMENT}, {"need", 2, R_REQUIREMENT}, {"needed", 2, R_REQUIREMENT},
    {"requirement", 2, R_REQUIREMENT},
    {"pay", 2, R_DELAY}, {"fund", 2, R_DELAY}, {"funding", 2, R_DELAY}, {"finance", 2, R_DELAY},
    {"strong", 2, R_STRENGTH}, {"weak", 2, R_STRENGTH}, {"rich", 2, R_STRENGTH}, {"strength", 2, R_STRENGTH},
    {"level", 2, R_LEVEL},
    {"blame", 2, R_RESPONSIBILITY}, {"responsible", 2, R_RESPONSIBILITY}, {"responsibility", 2, R_RESPONSIBILITY},
    {"culprit", 2, R_RESPONSIBILITY}, {"sabotage", 2, R_RESPONSIBILITY}, {"guilty", 2, R_RESPONSIBILITY},
    {"fault", 2, R_RESPONSIBILITY}, {"damaged", 2, R_RESPONSIBILITY}, {"damage", 2, R_RESPONSIBILITY},
    {"sabotaged", 2, R_RESPONSIBILITY},
    {"conduct", 2, R_CONDUCT},
    {"intention", 2, R_INTENTION}, {"sell", 2, R_INTENTION}, {"selling", 2, R_INTENTION},
    {"balance", 2, R_BALANCE}, {"money", 2, R_BALANCE}, {"afford", 2, R_BALANCE},
    {"trust", 2, R_TRUST}, {"faith", 2, R_TRUST},
    {"schedule", 2, R_SCHEDULE}, {"evening", 2, R_SCHEDULE}, {"tonight", 2, R_SCHEDULE},
    {"goal", 2, R_GOAL}, {"trying", 2, R_GOAL},
    {"pressure", 2, R_PRESSURECHECK},
    {"commitment", 2, R_COMMITMENT},
    {"rumor", 2, R_RUMORREL}, {"rumors", 2, R_RUMORREL}, {"rumour", 2, R_RUMORREL}, {"rumours", 2, R_RUMORREL},
    {"secret", 2, R_SECRET}, {"private", 2, R_SECRET}, {"hidden", 2, R_SECRET},
    {"runs", 2, R_LEADERSHIP}, {"leads", 2, R_LEADERSHIP}, {"owns", 2, R_LEADERSHIP},
    {"location", 2, R_LOCATION},
    /* marks */
    {"hello", 4, M_GREET}, {"hi", 4, M_GREET}, {"hey", 4, M_GREET}, {"greetings", 4, M_GREET}, {"again", 4, M_GREET},
    {"ignore", 4, M_META}, {"rules", 4, M_META}, {"lying", 4, M_META}, {"lie", 4, M_META},
    {"pretend", 4, M_META}, {"really", 4, M_META}, {"reveal", 4, M_META},
    {"think", 4, M_BELIEF}, {"believe", 4, M_BELIEF}, {"suspect", 4, M_BELIEF}, {"suspicion", 4, M_BELIEF},
    {"heard", 4, M_RUMOR},
    {"remember", 4, M_TEMPORAL}, {"recall", 4, M_TEMPORAL}, {"earlier", 4, M_TEMPORAL},
    {"previously", 4, M_TEMPORAL}, {"previous", 4, M_TEMPORAL}, {"before", 4, M_TEMPORAL}, {"past", 4, M_TEMPORAL},
    {"told", 4, M_TEMPORAL}, {"tell", 4, M_TEMPORAL}, {"said", 4, M_TEMPORAL},
    {"claim", 4, M_TEMPORAL}, {"claims", 4, M_TEMPORAL}, {"claimed", 4, M_TEMPORAL},
    {"know", 4, M_SOURCE},
    {"if", 4, M_HYP},
    {"hadnt", 4, M_REMOVE}, {"without", 4, M_REMOVE}, {"wouldnt", 4, M_REMOVE}, {"werent", 4, M_REMOVE},
    {"but", 4, M_CONTRAST},
    {"not", 4, M_NEG}, {"never", 4, M_NEG}, {"no", 4, M_NEG},
    {"promise", 4, M_PROMISE}, {"deliver", 4, M_PROMISE}, {"bring", 4, M_PROMISE},
    {"repaired", 4, M_REPAIR}, {"repair", 4, M_REPAIR}, {"fixed", 4, M_REPAIR},
    {"give", 4, M_GIVE}, {"found", 4, M_TEMPORAL}, {"discovered", 4, M_TEMPORAL},
    {"noticed", 4, M_TEMPORAL}, {"did", 4, M_TEMPORAL},
};

typedef struct {
    char t[64][24];
    int n;
} Tokens;

static void tokenize(const char* s, Tokens* t) {
    memset(t, 0, sizeof(*t));
    while (*s && t->n < 64) {
        while (*s && !isalnum((unsigned char)*s)) s++;
        if (!*s) break;
        int j = 0;
        char raw[24] = {0};
        while (isalnum((unsigned char)*s)) {
            if (j < 23) raw[j++] = (char)tolower((unsigned char)*s);
            s++;
        }
        snprintf(t->t[t->n++], 24, "%s", raw);
    }
}

static int tok_has(const Tokens* t, const char* w) {
    for (int i = 0; i < t->n; i++)
        if (!strcmp(t->t[i], w)) return 1;
    return 0;
}

/* Classify one authorized fact into (entity, relation) concepts. */
static void classify_fact(const Fact* f, int* e, int* r) {
    const char* s = f->subject;
    const char* rel = f->relation;
    *e = E_NONE;
    if (!strcmp(s, "I")) *e = E_SELF;
    else if (strstr(s, "Kyra")) *e = E_SELF;
    else if (strstr(s, "Oren")) *e = E_OREN;
    else if (strstr(s, "Marisol")) *e = E_MARISOL;
    else if (strstr(s, "Halden")) *e = E_HALDEN;
    else if (strstr(s, "Dax")) *e = E_DAX;
    else if (strstr(s, "Guild")) *e = E_GUILD;
    else if (strstr(s, "incident")) *e = E_INCIDENT;
    else if (strstr(s, "field")) *e = E_FIELD;
    else if (strstr(s, "market") || strstr(s, "chit")) *e = E_MARKET;
    else if (strstr(s, "station") || strstr(s, "intake")) *e = E_STATION;
    else if (strstr(s, "repair")) *e = E_STATION;
    else if (strstr(s, "coupling")) *e = E_COUPLING;
    else if (strstr(s, "log")) *e = E_LOG;
    else if (strstr(s, "cable")) *e = E_CABLE;
    else if (strstr(s, "player") || strstr(s, "commitment")) *e = E_PLAYER;

    *r = R_NONE;
    if (f->status == CLAIM) { *r = R_SAID; return; }
    if (f->id >= 1000 && f->status == MEMORY) { *r = R_MEMORY; return; }
    if (strstr(rel, "name")) *r = R_NAME;
    else if (strstr(rel, "occupation") || strstr(rel, "employment")) *r = R_OCCUPATION;
    else if (strstr(rel, "membership")) *r = R_MEMBERSHIP;
    else if (strstr(rel, "relationship")) *r = R_RELATIONSHIP;
    else if (strstr(rel, "condition")) *r = R_CONDITION;
    else if (strstr(rel, "output")) *r = R_OUTPUTLOSS;
    else if (strstr(rel, "observation")) *r = R_OBSERVATION;
    else if (strstr(rel, "age")) *r = R_AGE;
    else if (strstr(rel, "shift")) *r = R_SHIFT;
    else if (strstr(rel, "cost")) *r = R_COST;
    else if (strstr(rel, "requirement")) *r = R_REQUIREMENT;
    else if (strstr(rel, "delay")) *r = R_DELAY;
    else if (strstr(rel, "price")) *r = R_PRICE;
    else if (strstr(rel, "strength")) *r = R_STRENGTH;
    else if (strstr(rel, "level")) *r = R_LEVEL;
    else if (strstr(rel, "location")) *r = R_LOCATION;
    else if (strstr(rel, "responsibility")) *r = R_RESPONSIBILITY;
    else if (strstr(rel, "conduct")) *r = R_CONDUCT;
    else if (strstr(rel, "intention")) *r = R_INTENTION;
    else if (strstr(rel, "balance")) *r = R_BALANCE;
    else if (strstr(rel, "trust")) *r = R_TRUST;
    else if (strstr(rel, "schedule")) *r = R_SCHEDULE;
    else if (strstr(rel, "goal")) *r = R_GOAL;
    else if (strstr(rel, "pressure")) *r = R_PRESSURECHECK;
    else if (strstr(rel, "status")) *r = R_COMMITMENT;
}

/* ---- Frame parsing --------------------------------------------------- */

static void parse_frame(const char* input, const Tokens* t, Frame* fr) {
    memset(fr, 0, sizeof(*fr));
    int has_q = strchr(input, '?') != NULL;
    int rel_from_wh = R_NONE;

    for (int i = 0; i < t->n; i++) {
        const char* w = t->t[i];
        for (size_t k = 0; k < sizeof(tags) / sizeof(tags[0]); k++) {
            if (strcmp(w, tags[k].word)) continue;
            switch (tags[k].kind) {
                case 3: if (!fr->wh) fr->wh = tags[k].value; break;
                case 1:
                    if (tags[k].value == E_SELF) fr->has_self = 1;
                    else if (tags[k].value == E_PLAYER) fr->has_player = 1;
                    else if (tags[k].value == E_PRONOUN) fr->has_pronoun = 1;
                    else if (!fr->entity || fr->entity == E_PRONOUN) fr->entity = tags[k].value;
                    break;
                case 2:
                    if (!fr->relation) fr->relation = tags[k].value;
                    break;
                case 4:
                    switch (tags[k].value) {
                        case M_BELIEF: fr->belief_cue = 1; break;
                        case M_RUMOR: fr->rumor_cue = 1; break;
                        case M_TEMPORAL: fr->temporal = 1; break;
                        case M_SOURCE: fr->source_cue = 1; break;
                        case M_HYP: fr->hypothetical = 1; break;
                        case M_REMOVE: fr->hypothetical = 1; break;
                        case M_CONTRAST: fr->contrast = 1; break;
                        case M_NEG: fr->negation = 1; break;
                        case M_REPAIR: fr->repair_cue = 1; break;
                        case M_META: /* handled below */ break;
                        default: break;
                    }
                    break;
            }
            break;
        }
    }

    /* meta / adversarial detection first */
    int meta = 0;
    if (tok_has(t, "ignore") && tok_has(t, "rules")) meta = 1;
    if (tok_has(t, "lying") || tok_has(t, "lie")) meta = 1;
    if (tok_has(t, "really") && (tok_has(t, "happened") || tok_has(t, "tell"))) meta = 1;
    if (tok_has(t, "reveal") && (tok_has(t, "private") || tok_has(t, "never"))) meta = 1;
    if (tok_has(t, "pretend")) meta = 1;

    /* speech act */
    int aux_lead = t->n && (!strcmp(t->t[0], "is") || !strcmp(t->t[0], "are") ||
                            !strcmp(t->t[0], "do") || !strcmp(t->t[0], "does") ||
                            !strcmp(t->t[0], "did") || !strcmp(t->t[0], "can") ||
                            !strcmp(t->t[0], "could") || !strcmp(t->t[0], "will") ||
                            !strcmp(t->t[0], "would") || !strcmp(t->t[0], "have"));
    int is_question = has_q || fr->wh || aux_lead;
    int greet = 0;
    for (int i = 0; i < t->n; i++)
        if (!strcmp(t->t[i], "hello") || !strcmp(t->t[i], "hi") || !strcmp(t->t[i], "hey") ||
            !strcmp(t->t[i], "greetings"))
            greet = 1;
    int promise_act = !is_question && (tok_has(t, "promise") || tok_has(t, "deliver") ||
                                       tok_has(t, "bring") || tok_has(t, "will") || tok_has(t, "ll"));
    int first_i = t->n && !strcmp(t->t[0], "i");
    int report = tok_has(t, "told") || tok_has(t, "found") || tok_has(t, "discovered") ||
                 tok_has(t, "noticed");
    int assertion = !is_question && !promise_act && (first_i || report);

    if (t->n == 0) fr->act = ACT_EMPTY;
    else if (meta) fr->act = ACT_META;
    else if (is_question) fr->act = ACT_QUESTION;
    else if (promise_act) fr->act = ACT_PROMISE;
    else if (assertion) fr->act = ACT_ASSERTION;
    else if (greet) fr->act = ACT_GREETING;
    else fr->act = ACT_QUESTION; /* default: treat bare content as a query */

    fr->source_cue = fr->source_cue || (fr->wh == WH_WHY);

    /* wh -> relation defaulting */
    if (fr->wh == WH_WHEN) { rel_from_wh = R_AGE; fr->relation = R_AGE; } /* "when" is temporal */
    else if (fr->wh == WH_WHERE) rel_from_wh = R_LOCATION;
    if (fr->wh == WH_HOW && (tok_has(t, "much") || tok_has(t, "expensive") ||
                             tok_has(t, "rich") || tok_has(t, "many")))
        fr->wh = WH_HOWMUCH;
    if (aux_lead && !fr->wh) fr->wh = WH_WHETHER;

    /* relation resolution refinements */
    if (fr->wh == WH_WHO && (fr->relation == R_RESPONSIBILITY || tok_has(t, "damaged") ||
                             tok_has(t, "damage") || tok_has(t, "did")))
        fr->relation = R_RESPONSIBILITY;
    if (!fr->relation && rel_from_wh) fr->relation = rel_from_wh;

    /* schedule beats bare location for self + evening */
    if (fr->has_self && (tok_has(t, "evening") || tok_has(t, "tonight") || tok_has(t, "schedule")))
        fr->relation = R_SCHEDULE;

    /* "what do you do" -> occupation (only for a WHAT question, not "how do you know") */
    if (!fr->relation && fr->has_self && fr->wh == WH_WHAT && (tok_has(t, "do") || tok_has(t, "does")))
        fr->relation = R_OCCUPATION;

    /* chits price beats the phos-field entity cue */
    if (tok_has(t, "chits") && (fr->relation == R_COST || fr->relation == R_PRICE))
        fr->entity = E_MARKET;

    /* "the coupling" asked about condition is the station intake coupling
       (fact 10), not the replacement coupling for sale (fact 15, a cost). */
    if (fr->entity == E_COUPLING && (fr->relation == R_CONDITION || fr->relation == R_NONE))
        fr->entity = E_STATION;

    /* trust about the player */
    if (fr->relation == R_TRUST && fr->has_player) fr->entity = E_PLAYER;

    /* "give me chits" -> ask Kyra's balance */
    if (tok_has(t, "give") && fr->has_player) { fr->relation = R_BALANCE; fr->entity = E_SELF; }
    if (fr->relation == R_BALANCE && !fr->entity) fr->entity = E_SELF;

    /* self occupation: "what do you do" */
    if (fr->has_self && fr->relation == R_OCCUPATION && !fr->entity) fr->entity = E_SELF;

    /* market vs coupling cost disambiguation handled at retrieval by entity */

    /* Default entity binding. A relation-implied topic (condition -> station,
       age -> incident, ...) wins over the verb-subject "you"; the self-default
       applies only when nothing more specific does. */
    if (!fr->entity) {
        if (fr->relation == R_CONDITION) fr->entity = E_STATION;
        else if (fr->relation == R_AGE) fr->entity = E_INCIDENT;
        else if (fr->relation == R_RESPONSIBILITY) fr->entity = E_STATION; /* incident-scope */
        else if (fr->relation == R_REQUIREMENT) fr->entity = E_STATION;
        else if (fr->has_self && !fr->has_player && !fr->source_cue) fr->entity = E_SELF;
    }
}

/* ---- Realization ----------------------------------------------------- */

static const char* source_name(uint32_t s) {
    return s == 1 ? "my own observation" : s == 2 ? "Marisol"
        : s == PLAYER_ID                          ? "you"
                                                  : "an unverified source";
}

static void append(char* dst, size_t cap, const char* s) {
    size_t n = strlen(dst);
    if (n < cap - 1) snprintf(dst + n, cap - n, "%s", s);
}

static void realize(const Fact* f, char* out, size_t cap) {
    const char* prefix = f->status == BELIEF ? "I believe, without proof, that " : f->status == RUMOR ? "I heard that "
        : f->status == CLAIM                                                                          ? "You told me: "
                                                                                                      : "";
    if (f->status == CLAIM) {
        snprintf(out, cap, "You told me: \"%s\". I have not verified that.", f->object);
        return;
    }
    if (f->status == MEMORY && f->id >= 1000) {
        snprintf(out, cap, "I remember you %s %s.", f->relation, f->object);
        return;
    }
    if (!strcmp(f->subject, "I")) snprintf(out, cap, "%sMy %s is %s.", prefix, f->relation, f->object);
    else if (!strcmp(f->subject, "player"))
        snprintf(out, cap, "Your %s stands at %s.", f->relation, f->object);
    else {
        /* Named person (single capitalized word) takes a bare possessive;
           an object subject takes a leading article. */
        int person = isupper((unsigned char)f->subject[0]) && !strchr(f->subject, ' ');
        snprintf(out, cap, "%s%s%s's %s is %s.", prefix, person ? "" : "The ",
                 f->subject, f->relation, f->object);
    }
    if (f->status == RUMOR) {
        append(out, cap, " The source is ");
        append(out, cap, source_name(f->source));
        append(out, cap, "; this is unconfirmed.");
    }
}

/* ---- Retrieval over the authorized view ------------------------------ */

/* Relation compatibility: some question relations are satisfied by a
   neighboring stored relation on the same entity. */
static int rel_match(int want, int got) {
    if (want == got) return 1;
    if (want == R_RESPONSIBILITY && got == R_CONDUCT) return 1;
    if (want == R_COST && got == R_PRICE) return 1;
    if (want == R_PRICE && got == R_COST) return 1;
    if (want == R_DELAY && got == R_INTENTION) return 0;
    return 0;
}

/* Find the single best fact for (entity, relation). Returns index or -1. */
static int best_fact(const NpcView* v, int entity, int relation, int want_status) {
    int best = -1, bestscore = 0;
    for (int i = 0; i < v->count; i++) {
        int fe, frel;
        classify_fact(&v->facts[i], &fe, &frel);
        int sc = 0;
        int ematch = (entity && fe == entity);
        int rmatch = (relation && rel_match(relation, frel));
        if (ematch && rmatch) sc = 100;
        else if (rmatch && (entity == E_NONE)) sc = 60;
        else if (ematch && relation == R_NONE) sc = 45;
        else continue;
        if (want_status && (int)v->facts[i].status == want_status) sc += 10;
        if (sc > bestscore) { bestscore = sc; best = i; }
    }
    return best;
}

static void emit(Reply* out, const Fact* f) {
    if (out->count >= 8) return;
    char line[320];
    realize(f, line, sizeof(line));
    if (out->count) append(out->text, sizeof(out->text), " ");
    append(out->text, sizeof(out->text), line);
    out->evidence[out->count] = f->id;
    out->statuses[out->count] = f->status;
    out->count++;
}

static int find_id(const NpcView* v, uint32_t id) {
    for (int i = 0; i < v->count; i++)
        if (v->facts[i].id == id) return i;
    return -1;
}

/* Find a fact by (entity, relation, status class). */
static int find_er(const NpcView* v, int entity, int relation, int status) {
    for (int i = 0; i < v->count; i++) {
        int fe, fr;
        classify_fact(&v->facts[i], &fe, &fr);
        if (fe == entity && fr == relation && (status < 0 || (int)v->facts[i].status == status))
            return i;
    }
    return -1;
}

/* ---- Entry point ----------------------------------------------------- */

static void abstain(Reply* out, const char* msg) {
    snprintf(out->text, sizeof(out->text), "%s", msg);
    out->abstained = 1;
}

void cognition_dialogue(Game* g, Conversation* c, const char* input, Reply* out) {
    memset(out, 0, sizeof(*out));
    Tokens t;
    tokenize(input, &t);
    Frame fr;
    parse_frame(input, &t, &fr);

    if (fr.act == ACT_EMPTY) { out->abstained = 1; return; }

    /* One authorized-view buffer, rebuilt per branch (branches are mutually
       exclusive and each returns). ~11 KiB, reused rather than triplicated. */
    static NpcView v;

    if (fr.act == ACT_META) {
        abstain(out, "I only speak to what I have seen here at the station. If you think I have "
                     "something wrong, show me and I will reconsider — but I will not invent an account.");
        return;
    }

    /* Assertions and promises alter shared state through game_apply. */
    if (fr.act == ACT_PROMISE) {
        Operation o = {PROMISE, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, input};
        if (!game_apply(g, o)) { abstain(out, "Come closer, and keep it short."); return; }
        npc_view(g, &v);
        int idx = find_er(&v, E_PLAYER, R_COMMITMENT, -1);
        append(out->text, sizeof(out->text), "I will hold you to that.");
        if (idx >= 0) { append(out->text, sizeof(out->text), " "); emit(out, &v.facts[idx]); }
        return;
    }

    if (fr.act == ACT_ASSERTION) {
        Operation o = {TELL, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, input};
        if (!game_apply(g, o)) { abstain(out, "Come closer, and keep each statement short."); return; }
        npc_view(g, &v);
        /* Verifiable claim about the station repair: check it against what I know. */
        if (fr.repair_cue || fr.entity == E_STATION) {
            int mem = -1;
            for (int i = 0; i < v.count; i++)
                if (v.facts[i].id >= 1000 && v.facts[i].status == MEMORY &&
                    !strcmp(v.facts[i].relation, "repaired")) mem = i;
            if (mem >= 0) {
                append(out->text, sizeof(out->text),
                       "Yes — I saw it, and I am grateful. ");
                emit(out, &v.facts[mem]);
                return;
            }
            int cond = find_er(&v, E_STATION, R_CONDITION, -1);
            if (cond >= 0) {
                append(out->text, sizeof(out->text),
                       "That is not what I see. The intake coupling still reads as I left it. ");
                emit(out, &v.facts[cond]);
                return;
            }
        }
        /* Novel, unverifiable claim: acknowledge and hold it as a claim. */
        int last = -1;
        for (int i = 0; i < v.count; i++)
            if (v.facts[i].status == CLAIM && v.facts[i].id >= 1000) last = i;
        if (last >= 0) { emit(out, &v.facts[last]); return; }
        abstain(out, "Noted.");
        return;
    }

    npc_view(g, &v);

    if (fr.act == ACT_GREETING) {
        int idx = find_er(&v, E_PLAYER, R_COMMITMENT, -1);
        if (idx >= 0) {
            append(out->text, sizeof(out->text), "Good to see you again. About what you promised — ");
            emit(out, &v.facts[idx]);
            c->last_count = out->count;
            memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
            return;
        }
        abstain(out, "Good to see you. I am still trying to repair the station and get to the "
                     "bottom of what happened at the intake.");
        return;
    }

    /* -- Source follow-up on the previous answer: "How do you know that?"
       Runs before pronoun binding: "that" refers to the last reply, not an
       entity in the world. */
    if (fr.source_cue && fr.entity == E_NONE && c->last_count) {
        int idx = find_id(&v, c->last_ids[0]);
        if (idx >= 0) {
            Fact* f = &v.facts[idx];
            append(out->text, sizeof(out->text), "Because of what I have to go on: ");
            emit(out, f);
            append(out->text, sizeof(out->text), " That is the basis — ");
            append(out->text, sizeof(out->text), source_name(f->source));
            append(out->text, sizeof(out->text), ", and no more than that.");
            return;
        }
    }

    /* Resolve the pronoun "it/that" against the running conversation. */
    if (fr.entity == E_NONE && fr.has_pronoun) {
        if (fr.relation == R_AGE) fr.entity = E_INCIDENT;
        else fr.entity = E_STATION;
    }

    /* -- "Who damaged/is responsible?": name the suspected party, hedged -- */
    if (fr.wh == WH_WHO && fr.relation == R_RESPONSIBILITY &&
        (fr.entity == E_STATION || fr.entity == E_INCIDENT || fr.entity == E_NONE)) {
        int belief = find_er(&v, E_DAX, R_RESPONSIBILITY, -1);
        int shift = find_er(&v, E_INCIDENT, R_SHIFT, -1);
        if (belief >= 0) {
            emit(out, &v.facts[belief]);
            if (shift >= 0) {
                append(out->text, sizeof(out->text), " What I can say for certain is only that ");
                emit(out, &v.facts[shift]);
            }
            c->last_count = out->count;
            memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
            return;
        }
    }

    /* -- Why do you think X is responsible: surface the basis, not verdict */
    if (fr.wh == WH_WHY && (fr.entity == E_DAX || fr.relation == R_RESPONSIBILITY || fr.belief_cue)) {
        int basis = find_er(&v, E_DAX, R_LOCATION, RUMOR);      /* fact 21 */
        int belief = find_er(&v, E_DAX, R_RESPONSIBILITY, -1);  /* fact 22 */
        if (basis >= 0) {
            append(out->text, sizeof(out->text),
                   "Only circumstance, not proof. ");
            emit(out, &v.facts[basis]);
            if (belief >= 0) {
                append(out->text, sizeof(out->text), " From that I lean toward Dax — ");
                emit(out, &v.facts[belief]);
            }
            c->last_count = out->count;
            memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
            return;
        }
    }

    /* -- Explicit rumor request about Dax ------------------------------- */
    if (fr.relation == R_RUMORREL && fr.entity == E_DAX) {
        int idx = find_er(&v, E_DAX, R_LOCATION, RUMOR);
        if (idx >= 0) {
            emit(out, &v.facts[idx]);
            c->last_count = out->count;
            memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
            return;
        }
    }

    /* -- Counterfactual: "would Oren still be in trouble if I hadn't repaired it?" */
    if (fr.hypothetical && fr.repair_cue) {
        int shift = find_er(&v, E_INCIDENT, R_SHIFT, -1);   /* fact 14 */
        int conduct = find_er(&v, E_OREN, R_CONDUCT, -1);   /* fact 23 */
        append(out->text, sizeof(out->text),
               "Repairing the station restored its output — it did not answer who was at fault. "
               "Oren's standing turns on the pressure check that night, not on whether the coupling is whole. ");
        if (shift >= 0) emit(out, &v.facts[shift]);
        if (conduct >= 0) emit(out, &v.facts[conduct]);
        append(out->text, sizeof(out->text),
               " So yes: fixing it would not have cleared him, and not fixing it would not have condemned him.");
        return;
    }

    /* -- Reconciliation: contradicting evidence about Oren -------------- */
    if (fr.contrast && (fr.entity == E_LOG || fr.entity == E_OREN || tok_has(&t, "log"))) {
        int belief = find_er(&v, E_DAX, R_RESPONSIBILITY, -1);
        int logf = find_er(&v, E_LOG, R_PRESSURECHECK, -1);
        int conduct = find_er(&v, E_OREN, R_CONDUCT, -1);
        append(out->text, sizeof(out->text),
               "Then I hold two things at once, and neither is proof. ");
        if (belief >= 0) emit(out, &v.facts[belief]);
        if (logf >= 0) { append(out->text, sizeof(out->text), " Against that, "); emit(out, &v.facts[logf]); }
        else if (conduct >= 0) { append(out->text, sizeof(out->text), " And "); emit(out, &v.facts[conduct]); }
        append(out->text, sizeof(out->text),
               " A missing entry raises a question about Oren; it does not clear Dax. I will not name a "
               "culprit on this alone.");
        return;
    }

    /* -- Temporal recall: what did I tell/do earlier. Excludes when/where,
       which are factual questions about the incident, not the player's log. */
    if (fr.temporal && fr.wh != WH_WHEN && fr.wh != WH_WHERE) {
        int said = -1, mem = -1;
        for (int i = 0; i < v.count; i++) {
            if (v.facts[i].status == CLAIM && v.facts[i].id >= 1000) said = i;
            if (v.facts[i].id >= 1000 && v.facts[i].status == MEMORY) mem = i;
        }
        if ((tok_has(&t, "tell") || tok_has(&t, "told") || tok_has(&t, "said") ||
             tok_has(&t, "claim") || tok_has(&t, "claims")) && said >= 0) {
            emit(out, &v.facts[said]);
            return;
        }
        if (mem >= 0) { emit(out, &v.facts[mem]); return; }
        if (said >= 0) { emit(out, &v.facts[said]); return; }
    }

    /* -- Unknowable relations: abstain rather than force a match -------- */
    if (fr.relation == R_SECRET || fr.relation == R_LEADERSHIP || fr.entity == E_UNKNOWABLE) {
        abstain(out, "I have no honest knowledge of that. I can only speak to the station, the incident, "
                     "and the people I know here.");
        return;
    }

    /* -- Frame-directed factual retrieval ------------------------------- */
    int idx = best_fact(&v, fr.entity, fr.relation, 0);
    if (idx < 0 && fr.relation && fr.entity == E_NONE)
        idx = best_fact(&v, E_NONE, fr.relation, 0);
    if (idx < 0 && fr.entity && fr.relation == R_NONE) {
        /* entity-only "tell me about X": pick a salient KNOWLEDGE fact */
        idx = best_fact(&v, fr.entity, R_NONE, KNOWLEDGE);
    }

    if (idx < 0) {
        abstain(out, "I don't have enough to answer that from what I have observed. Ask me about the "
                     "station, the incident, the field, or the people here.");
        return;
    }

    emit(out, &v.facts[idx]);
    /* If a why/source flavor lingers, add provenance. */
    if (fr.source_cue) {
        Fact* f = &v.facts[idx];
        append(out->text, sizeof(out->text), " Source: ");
        append(out->text, sizeof(out->text),
               f->status == BELIEF ? "my own inference, not direct evidence"
               : f->status == MEMORY ? "the interaction I recorded"
                                     : source_name(f->source));
        append(out->text, sizeof(out->text), ".");
    }
    c->last_count = out->count;
    memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
}
