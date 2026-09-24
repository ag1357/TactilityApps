#include "state.h"
#include <string.h>

/* Directed social evidence and its projections (Gate 6). Records are the
   retained causal capsules; views are pure functions of (records, clock).
   Nothing here ranks people: each answer is one directed observer->subject
   estimate with its counts, and an observer without records is unchanged.

   Decay is a fixed integer bucket per record, evaluated against the stored
   anchor: bucket = (clock_s - anchor) / WS_EV_DECAY_S, weight = confidence
   >> bucket. Query frequency cannot change forgiveness because no state is
   mutated at query time; save/reload reproduces answers because the anchors
   round-trip. High-salience records are pinned (bucket 0 forever) so grave
   history and active obligations survive decay. Reported evidence can never
   exceed WS_EV_REPORTED_MAX confidence regardless of how many hands it
   passed: copying is not corroboration. */

enum { WS_EV_DECAY_S = 86400, WS_EV_REPORTED_MAX = 750, WS_EV_REPORTED_SHARE = 3 };

/* Handle vocabulary: NPC = entity index + 1; player = WS_EV_PLAYER + index.
   Shared by evidence formation and state validation so the two can never
   disagree about who a record may name. */
int ws_ev_handle_ok(const WsState* s, const WsRecipe* r, uint32_t h) {
    if (!h) return 0;
    if (h >= WS_EV_PLAYER) {
        unsigned pi = h - WS_EV_PLAYER;
        return pi < s->player_count && s->players[pi].id != 0;
    }
    return h <= r->count && (r->modules[h - 1].flags & WS_NPC);
}

void ws_evidence_put(WsState* s, const WsEvidence* e) {
    /* Same (observer, root, teller, kind) replaces instead of appending:
       a repeated report or projection is the same cause, not independent
       corroboration, so it can move a record but never stack copies. */
    for (int i = 0; i < s->ev_count; i++) {
        WsEvidence* at = &s->ev[i];
        if (at->observer == e->observer && at->root == e->root && at->teller == e->teller && at->kind == e->kind) {
            *at = *e;
            return;
        }
    }
    if (s->ev_count == WS_EV_CAP) {
        memmove(s->ev, s->ev + 1, sizeof(*s->ev) * (WS_EV_CAP - 1));
        s->ev_count--;
    }
    s->ev[s->ev_count++] = *e;
}

WsError ws_inform(WsState* s, const WsRecipe* r, uint32_t observer, uint32_t subject, uint32_t teller, uint16_t kind, uint16_t confidence, uint16_t context, int16_t valence, uint16_t salience, uint32_t root) {
    if (!s || !r) return WS_BOUNDS;
    ws_social_settle(s, r);
    if (kind < WS_EV_ACT || kind > WS_EV_REPORT) return WS_BOUNDS;
    if (confidence > 1000 || salience > 1000 || context > WS_CTX_DISPATCH) return WS_BOUNDS;
    if (valence < -1000 || valence > 1000) return WS_BOUNDS;
    if (!ws_ev_handle_ok(s, r, observer) || !ws_ev_handle_ok(s, r, subject)) return WS_REFERENCE;
    if (teller && !ws_ev_handle_ok(s, r, teller)) return WS_REFERENCE;
    if (teller == observer) return WS_BOUNDS; /* self-report is not evidence */
    if (!root || root > s->revision) return WS_REFERENCE;
    /* All record fields are pre-checked, so the mutation keeps the state
       valid by construction (the creature-exception pattern); the proof
       gates validate after every inform. */
    WsEvidence e = {observer, subject, teller, root, kind, confidence, context, s->clock_s, salience, 0, valence, 0};
    ws_evidence_put(s, &e);
    return WS_OK;
}

/* Interpretation before aggregation: consensual (arena) or defensive force
   counts at half weight; a restitution context doubles its own positive
   contribution. Claims carry no valence at all: they are counted, never
   believed. */
static int16_t effective(const WsEvidence* e) {
    int32_t v = e->valence;
    if ((e->context == WS_CTX_ARENA || e->context == WS_CTX_DEFENSE) && v < 0) v /= 2;
    if (e->context == WS_CTX_RESTITUTION && v > 0) v *= 2;
    if (v > 1000) v = 1000;
    if (v < -1000) v = -1000;
    return (int16_t)v;
}

int ws_social_view(const WsState* s, uint32_t observer, uint32_t subject, WsView* out) {
    if (!s || !out) return 0;
    memset(out, 0, sizeof(*out));
    int32_t trust = 0;
    uint32_t confidence = 0, watermark = 0;
    for (int i = 0; i < s->ev_count && i < WS_EV_CAP; i++) {
        const WsEvidence* e = &s->ev[i];
        if (e->observer != observer || e->subject != subject) continue;
        unsigned bucket = e->salience >= WS_EV_PINNED ? 0 : (unsigned)((s->clock_s - e->clock_s) / WS_EV_DECAY_S);
        if (bucket > 15) continue; /* minor history beyond the window is gone */
        uint32_t weight = (uint32_t)e->confidence >> bucket;
        trust += (int32_t)effective(e) * (int32_t)weight / 1000;
        confidence += weight;
        if (e->root > watermark) watermark = e->root;
        switch (e->kind) {
            case WS_EV_ACT: out->acts++; break;
            case WS_EV_CLAIM: out->claims++; break;
            case WS_EV_CONTRADICT: out->contradictions++; break;
            case WS_EV_RESTITUTION: out->restitutions++; break;
            case WS_EV_PROMISE: out->promises++; break;
            case WS_EV_KEPT: out->kept++; break;
            case WS_EV_BREACH: out->breaches++; break;
            case WS_EV_REPORT: out->reports++; break;
            default: break;
        }
    }
    if (trust > 1000) trust = 1000;
    if (trust < -1000) trust = -1000;
    out->trust = (int16_t)trust;
    out->confidence = (uint16_t)(confidence > 1000 ? 1000 : confidence);
    out->watermark = watermark;
    return 1;
}

int ws_social_cite(const WsState* s, uint32_t observer, uint32_t subject, uint32_t* root, uint16_t* kind, uint16_t* salience) {
    if (!s) return 0;
    int found = 0;
    uint32_t best_root = 0;
    uint16_t best_kind = 0, best_salience = 0;
    for (int i = 0; i < s->ev_count && i < WS_EV_CAP; i++) {
        const WsEvidence* e = &s->ev[i];
        if (e->observer != observer || e->subject != subject) continue;
        /* Strongest retained cause first; ties resolve to the latest
           committed root so the citation stays stable under eviction. */
        if (!found || e->salience > best_salience || (e->salience == best_salience && e->root > best_root)) {
            found = 1;
            best_root = e->root;
            best_kind = e->kind;
            best_salience = e->salience;
        }
    }
    if (root) *root = best_root;
    if (kind) *kind = best_kind;
    if (salience) *salience = best_salience;
    return found;
}

/* Authority-side institutional filing: an observer (NPC or player handle)
   submits its strongest retained evidence about a subject to a clerk NPC
   for delayed delivery. This is the NPC-to-institution information edge:
   Kyra can file what she actually observed, and the clerk's eventual
   record keeps the original cause's root while its confidence degrades. */
WsError ws_file(WsState* s, const WsRecipe* r, uint32_t clerk, uint32_t observer, uint32_t subject) {
    if (!s || !r) return WS_BOUNDS;
    ws_social_settle(s, r);
    if (!ws_ev_handle_ok(s, r, clerk) || clerk >= WS_EV_PLAYER) return WS_REFERENCE; /* clerks are NPC entities */
    if (!ws_ev_handle_ok(s, r, observer) || !ws_ev_handle_ok(s, r, subject)) return WS_REFERENCE;
    if (observer == clerk || subject == clerk) return WS_BOUNDS;
    int found = 0;
    WsPending pend = {0};
    for (int i = 0; i < s->ev_count; i++) {
        const WsEvidence* v = &s->ev[i];
        if (v->observer != observer || v->subject != subject) continue;
        if (!found || v->salience > pend.salience || (v->salience == pend.salience && v->root > pend.root)) {
            found = 1;
            pend = (WsPending) {clerk, subject, observer, v->root, s->clock_s + WS_REPORT_DELAY_S,
                                v->kind, v->confidence, v->context, v->salience, 0, v->valence, 0};
        }
    }
    if (!found) return WS_DENIED; /* nothing retained: nothing to file */
    int at = -1;
    for (int i = 0; i < s->pend_count; i++)
        if (s->pend[i].observer == clerk && s->pend[i].teller == observer && s->pend[i].subject == subject) {
            at = i;
            break;
        }
    if (at < 0) {
        if (s->pend_count >= WS_PEND_CAP) return WS_FULL;
        at = s->pend_count++;
    }
    s->pend[at] = pend;
    return WS_OK;
}

void ws_clock_advance(WsState* s, const WsRecipe* r, uint32_t delta_s) {
    if (!s || !r || !delta_s || delta_s > 30u * 86400u) return;
    s->clock_s += delta_s;
    ws_social_settle(s, r);
}

void ws_social_settle(WsState* s, const WsRecipe* r) {
    if (!s || !r) return;
    for (int i = 0; i < s->pend_count;) {
        WsPending* p = &s->pend[i];
        if ((int64_t)s->clock_s < (int64_t)p->deliver_s) {
            i++;
            continue;
        }
        /* Delivery is a canonical institutional event: the clerk gains
           degraded REPORT-class evidence (a report is not the act), and the
           public feed records the faction's receipt with its own revision.
           Pending dispatches are player-filed, so the feed actor resolves
           through the player table. */
        WsEvidence e = {p->observer, p->subject, p->teller, p->root, WS_EV_REPORT,
                        (uint16_t)((uint32_t)p->confidence * WS_EV_REPORTED_SHARE / 4 > WS_EV_REPORTED_MAX ? WS_EV_REPORTED_MAX : p->confidence * WS_EV_REPORTED_SHARE / 4),
                        p->context, s->clock_s, p->salience, 0, p->valence, 0};
        uint32_t clerk = p->observer, teller = p->teller;
        memmove(s->pend + i, s->pend + i + 1, sizeof(*s->pend) * (size_t)(s->pend_count - i - 1));
        s->pend_count--;
        if (s->revision == UINT32_MAX) continue;
        s->revision++;
        uint32_t actor = teller >= WS_EV_PLAYER && teller - WS_EV_PLAYER < s->player_count ? s->players[teller - WS_EV_PLAYER].id : 0;
        WsFeed notice = {s->revision, actor, s->revision, (uint16_t)(clerk - 1), WS_FACTION_NOTICE};
        ws_evidence_put(s, &e);
        if (s->feed_count == WS_FEED_CAP) {
            memmove(s->feed, s->feed + 1, sizeof(*s->feed) * (WS_FEED_CAP - 1));
            s->feed_count--;
        }
        s->feed[s->feed_count++] = notice;
    }
}
