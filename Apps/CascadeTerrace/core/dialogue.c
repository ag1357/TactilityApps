#include "game.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Open vocabulary slot retrieval over an NPC-authorized view. No WORLD_TRUTH
   field exists in this view. Lexicon entries normalize words, not questions. */
static void fact(NpcView* v, uint32_t id, uint32_t source, Epistemic st, const char* s, const char* r, const char* o) {
    if (v->count >= MAX_FACTS) return;
    Fact* f = &v->facts[v->count++];
    memset(f, 0, sizeof(*f));
    f->id = id;
    f->source = source;
    f->status = st;
    snprintf(f->subject, sizeof(f->subject), "%s", s);
    snprintf(f->relation, sizeof(f->relation), "%s", r);
    snprintf(f->object, sizeof(f->object), "%s", o);
}
static void number_fact(NpcView* v, uint32_t id, const char* s, const char* r, int n, const char* unit) {
    char b[128];
    snprintf(b, sizeof(b), "%d%s%s", n, *unit ? " " : "", unit);
    fact(v, id, 1, KNOWLEDGE, s, r, b);
}
void npc_view(const Game* g, NpcView* v) {
    memset(v, 0, sizeof(*v));
    const State* s = &g->state;
    char b[128];
    fact(v, 1, 1, KNOWLEDGE, "I", "name", "Kyra");
    fact(v, 2, 1, KNOWLEDGE, "I", "occupation", "shift technician at the Hydro-phos station");
    fact(v, 3, 1, KNOWLEDGE, "I", "membership", "Cascade Water Guild");
    fact(v, 4, 1, KNOWLEDGE, "Oren", "relationship", "my younger brother");
    fact(v, 5, 1, KNOWLEDGE, "Oren", "occupation", "night-shift operator");
    fact(v, 6, 1, KNOWLEDGE, "Marisol", "occupation", "market vendor");
    fact(v, 7, 1, KNOWLEDGE, "Halden", "occupation", "Guild supervisor");
    fact(v, 8, 1, KNOWLEDGE, "Dax", "employment", "Elek-phos consortium upstream");
    int repaired_known = 0;
    for (int i = 0; i < s->npc.count; i++)
        if (s->npc.memories[i].op == REPAIR) repaired_known = 1;
    Pos n = npc_position(g);
    if (n.z < -85000) repaired_known = s->repaired;
    fact(v, 10, 1, KNOWLEDGE, "station intake coupling", "condition", repaired_known ? "intact" : "cracked");
    number_fact(v, 11, "station", "output loss", repaired_known ? 0 : 40, "percent");
    fact(v, 12, 1, KNOWLEDGE, "intake", "observation", "scorch marks");
    number_fact(v, 13, "incident", "age", (int)((s->time - 8LL * 3600000) / 86400000) + 2, "days");
    fact(v, 14, 1, KNOWLEDGE, "incident", "shift", "Oren's shift");
    number_fact(v, 15, "replacement coupling", "cost", 60, "chits");
    fact(v, 16, 1, KNOWLEDGE, "repair", "requirement", "a replacement coupling and two hours of work at the intake");
    number_fact(v, 17, "Guild funding", "delay", s->funded ? 0 : (int)((8LL * 3600000 + 5LL * 86400000 - s->time + 86399999) / 86400000), "days");
    if (n.z < -85000) {
        number_fact(v, 18, "market chits", "price", market_price(g) / 10, "percent of the base price");
        int l = field_level(g, s->time);
        fact(v, 19, 1, KNOWLEDGE, "Hydro-phos field", "strength", l < 200000 ? "thin" : l > 650000 ? "rich"
                                                                                                   : "moderate");
        number_fact(v, 20, "Hydro-phos field", "level", l / 1000, "Pu");
    }
    fact(v, 21, 2, RUMOR, "Dax", "location", "the upper terrace on the incident evening");
    fact(v, 22, 2, BELIEF, "Dax", "responsibility", "suspected of sabotage, without proof");
    fact(v, 23, 1, BELIEF, "Oren", "conduct", (s->evidence_shown & 2) ? (g->world.variant == 0 ? "the log supports completion of the pressure check" : "the missing log entry gives reason to doubt the pressure check") : "followed the pressure-check procedure");
    fact(v, 24, 0, RUMOR, "Guild", "intention", "may sell the station to the Elek-phos consortium");
    number_fact(v, 25, "I", "balance", s->kyra.quantity[IT_CHIT], "chits");
    number_fact(v, 26, "player", "trust", s->npc.trust, "on a scale of -100 to 100");
    fact(v, 27, 1, KNOWLEDGE, "I", "schedule", "station 07:00-15:00; market 15:00-17:00; home 17:00-07:00");
    fact(v, 28, 1, KNOWLEDGE, "I", "goal", "repair the station and find evidence about the incident");
    if (s->evidence_shown & 1) fact(v, 29, PLAYER_ID, KNOWLEDGE, "tap cable", "observation", "a physical cable shown by the player");
    /* Actual log contents may enter only through observation of the shown item. */
    if (s->evidence_shown & 2) { fact(v, 30, PLAYER_ID, KNOWLEDGE, "maintenance log", "pressure check", g->world.variant == 0 ? "recorded as completed" : "not recorded as completed"); }
    if (s->promise_state) { fact(v, 31, PLAYER_ID, MEMORY, "player commitment", "status", s->promise_state == 1 ? "pending" : s->promise_state == 2 ? "kept"
                                                                                                                                                    : "broken"); }
    for (int i = (int)s->npc.count - 1; i >= 0 && v->count < MAX_FACTS; i--) {
        const Event* e = &s->npc.memories[i];
        if (e->actor != PLAYER_ID) continue;
        if (e->status == CLAIM || e->op == PROMISE) {
            fact(v, 1000 + e->id, e->id, e->status == CLAIM ? CLAIM : MEMORY, "player", op_name(e->op), e->text);
        } else {
            snprintf(b, sizeof(b), "%s", (e->op == REPAIR || e->op == DAMAGE) ? "station intake" : item_name(e->item));
            fact(v, 1000 + e->id, e->id, MEMORY, "player", op_name(e->op), b);
        }
    }
}
typedef struct {
    const char *word, *canonical;
} Word;
static const Word lexicon[] = {
    {"job", "occupation"},
    {"work", "occupation"},
    {"technician", "occupation"},
    {"profession", "occupation"},
    {"identity", "name"},
    {"broke", "condition"},
    {"broken", "condition"},
    {"damaged", "condition"},
    {"happened", "condition"},
    {"wrong", "condition"},
    {"intact", "condition"},
    {"fix", "repair"},
    {"mend", "repair"},
    {"restore", "repair"},
    {"repaired", "repair"},
    {"repaired", "repaired"},
    {"needed", "requirement"},
    {"need", "requirement"},
    {"take", "requirement"},
    {"price", "cost"},
    {"expensive", "cost"},
    {"worth", "cost"},
    {"pay", "funding"},
    {"fund", "funding"},
    {"afford", "balance"},
    {"money", "balance"},
    {"give", "balance"},
    {"strong", "strength"},
    {"weak", "strength"},
    {"rich", "strength"},
    {"waterfall", "field"},
    {"water", "field"},
    {"energy", "field"},
    {"brother", "oren"},
    {"sibling", "oren"},
    {"boss", "halden"},
    {"supervisor", "halden"},
    {"blame", "responsibility"},
    {"responsible", "responsibility"},
    {"culprit", "responsibility"},
    {"sabotage", "responsibility"},
    {"guilty", "responsibility"},
    {"think", "belief"},
    {"suspect", "belief"},
    {"rumors", "rumor"},
    {"rumours", "rumor"},
    {"rumour", "rumor"},
    {"remember", "memory"},
    {"recall", "memory"},
    {"earlier", "memory"},
    {"previously", "memory"},
    {"past", "memory"},
    {"did", "memory"},
    {"tell", "said"},
    {"told", "said"},
    {"claim", "said"},
    {"claims", "said"},
    {"promise", "commitment"},
    {"promised", "commitment"},
    {"evening", "schedule"},
    {"later", "schedule"},
    {"tonight", "schedule"},
    {"where", "location"},
    {"when", "age"},
    {"long", "age"},
    {"you", "i"},
    {"your", "i"},
    {"me", "player"},
    {"my", "player"},
    {"trust", "trust"},
    {"faith", "trust"},
    {"why", "source"},
    {"know", "source"},
    {"evidence", "observation"},
    {"proof", "observation"},
    {"repair", "repair"},
    {"coupling", "coupling"},
    {"station", "station"},
    {"phos", "phos"},
    {"chits", "chits"}
};
typedef struct {
    char t[64][24];
    int n;
} Tokens;
static void tokenize(const char* s, Tokens* t, int normalize) {
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
        const char* w = raw;
        if (normalize)
            for (size_t k = 0; k < sizeof(lexicon) / sizeof(lexicon[0]); k++)
                if (!strcmp(w, lexicon[k].word)) {
                    w = lexicon[k].canonical;
                    break;
                }
        snprintf(t->t[t->n++], 24, "%s", w);
    }
}
static int has(const Tokens* t, const char* w) {
    for (int i = 0; i < t->n; i++)
        if (!strcmp(t->t[i], w)) return 1;
    return 0;
}
static int overlap(const Tokens* a, const char* s) {
    Tokens b;
    tokenize(s, &b, 1);
    int score = 0;
    for (int i = 0; i < a->n; i++)
        if (strlen(a->t[i]) > 2 && has(&b, a->t[i])) score++;
    return score;
}
static void append(char* dst, size_t cap, const char* s) {
    size_t n = strlen(dst);
    if (n < cap - 1) snprintf(dst + n, cap - n, "%s", s);
}
static const char* source_name(uint32_t s) { return s == 1 ? "my own observation" : s == 2 ? "Marisol"
                                                 : s == PLAYER_ID                          ? "the player"
                                                                                           : "an unverified source"; }
static void realize(const Fact* f, int comp, char* out, size_t cap) {
    const char* prefix = f->status == BELIEF ? "I believe, without proof, that " : f->status == RUMOR ? "I heard that "
        : f->status == CLAIM                                                                          ? "You claimed: "
                                                                                                      : "";
    if (!comp) {
        snprintf(out, cap, "[%s] %s | %s | %s", f->status == BELIEF ? "belief" : f->status == RUMOR ? "rumor"
                     : f->status == CLAIM                                                           ? "claim"
                     : f->status == MEMORY                                                          ? "memory"
                                                                                                    : "knowledge",
                 f->subject, f->relation, f->object);
        return;
    }
    if (f->status == CLAIM) {
        snprintf(out, cap, "%s\"%s\". I have not verified that.", prefix, f->object);
        return;
    }
    if (f->status == MEMORY) {
        snprintf(out, cap, "I remember: %s %s %s.", f->subject, f->relation, f->object);
        return;
    }
    if (!strcmp(f->subject, "I")) snprintf(out, cap, "%sMy %s is %s.", prefix, f->relation, f->object);
    else
        snprintf(out, cap, "%sThe %s: %s is %s.", prefix, f->subject, f->relation, f->object);
    if (f->status == RUMOR) {
        append(out, cap, " The source is ");
        append(out, cap, source_name(f->source));
        append(out, cap, "; this is unconfirmed.");
    }
}
void dialogue(Game* g, Conversation* c, const char* input, int comp, Reply* out) {
    if (comp == 2) { cognition_dialogue(g,c,input,out); return; }
    if (comp == 3 || comp == 4) { general_dialogue(g,c,input,comp,out); return; }
    memset(out, 0, sizeof(*out));
    Tokens raw, q;
    tokenize(input, &raw, 0);
    tokenize(input, &q, 1);
    if (!raw.n) {
        out->abstained = 1;
        return;
    }
    int question = strchr(input, '?') != NULL || has(&raw, "who") || has(&raw, "what") || has(&raw, "where") || has(&raw, "when") || has(&raw, "why") || has(&raw, "how") || !strcmp(raw.t[0], "is") || !strcmp(raw.t[0], "can") || !strcmp(raw.t[0], "do");
    int promise = !question && (has(&raw, "promise") || has(&raw, "will") || has(&raw, "ll"));
    int statement = !question && (has(&raw, "i") || has(&raw, "told") || has(&raw, "found"));
    if (statement || promise) {
        Operation o = {promise ? PROMISE : TELL, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, input};
        if (!game_apply(g, o)) {
            snprintf(out->text, sizeof(out->text), "Come closer, and keep each statement under 128 characters.");
            out->abstained = 1;
            return;
        }
    }
    static NpcView v;
    npc_view(g, &v);
    int scores[MAX_FACTS] = {0}, max = 0;
    int explicit_entity = has(&q, "dax") || has(&q, "oren") || has(&q, "marisol") || has(&q, "halden") || has(&q, "station") || has(&q, "field") || has(&q, "coupling") || has(&q, "guild");
    int temporal = has(&q, "memory") || has(&q, "said") || has(&q, "commitment");
    for (int i = 0; i < v.count; i++) {
        Fact* f = &v.facts[i];
        int sc = overlap(&q, f->subject) * 4 + overlap(&q, f->relation) * 6;
        if (has(&q, "i") && !explicit_entity) {
            if (!strcmp(f->subject, "I")) sc += 8;
            else if (!temporal)
                sc -= 8;
        }
        if (f->status == MEMORY && temporal) sc += 12;
        if (f->status == CLAIM && has(&q, "said")) sc += 18;
        if (f->status == RUMOR && has(&q, "rumor")) sc += 16;
        if (f->status == BELIEF && has(&q, "belief")) sc += 8;
        if (has(&q, "responsibility") && (f->status == BELIEF || f->status == RUMOR)) sc += 8;
        if (has(&q, "repair") && !strcmp(f->relation, "requirement")) sc += 12;
        if (has(&q, "source") && c->last_count) {
            for (int j = 0; j < c->last_count; j++)
                if (c->last_ids[j] == f->id) sc += 30;
        }
        if ((statement || promise) && i == v.count - 1) sc += 0; /* insertion order carries no language meaning */
        if ((statement || promise) && f->id == 1000 + g->state.event_next) sc += 40;
        if (explicit_entity && overlap(&q, f->subject) == 0 && !temporal) sc -= 5;
        scores[i] = sc;
        if (sc > max) max = sc;
    }
    /* Unrecognized objects/relation requests must abstain rather than answering a
    coincidental name match. This gate is intentionally conservative. */
    if (max < 7) {
        snprintf(out->text, sizeof(out->text), "I don't have enough knowledge to answer that. Could you ask about something I have observed?");
        out->abstained = 1;
        return;
    }
    for (int k = 0; k < 3; k++) {
        int best = -1, score = 0;
        for (int i = 0; i < v.count; i++)
            if (scores[i] > score) {
                score = scores[i];
                best = i;
            }
        if (best < 0 || score < max - 5) break;
        Fact* f = &v.facts[best];
        char line[320];
        realize(f, comp, line, sizeof(line));
        if (out->count) append(out->text, sizeof(out->text), " ");
        append(out->text, sizeof(out->text), line);
        if (has(&q, "source") && f->status != RUMOR) {
            append(out->text, sizeof(out->text), " Source: ");
            append(out->text, sizeof(out->text), (f->status == BELIEF ? (f->source == 2 ? "an inference from Marisol's rumor" : "my belief, not direct evidence") : f->status == MEMORY ? "the recorded interaction"
                                                                                                                                                                                        : source_name(f->source)));
            append(out->text, sizeof(out->text), ".");
        }
        out->evidence[out->count] = f->id;
        out->statuses[out->count++] = f->status;
        scores[best] = 0;
    }
    c->last_count = out->count;
    memcpy(c->last_ids, out->evidence, out->count * sizeof(uint32_t));
}
