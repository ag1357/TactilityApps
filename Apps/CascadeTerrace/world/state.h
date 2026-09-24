#ifndef CT_WORLD_STATE_H
#define CT_WORLD_STATE_H
#include "sdk.h"
#define WS_INVENTORY 7
#define WS_FEED_CAP 16
/* Indices are product-local handles; every envelope binds the ancestry and CRC. */
typedef enum { WS_EXTRACT = 1,
               WS_REPAIR,
               WS_DAMAGE,
               WS_TRANSFER,
               WS_GRANT,
               WS_DECORATE,
               WS_AID,
               WS_KILL,
               WS_PROMISE,
               WS_KEEP_PROMISE,
               /* Resource ops target reservoir indices, not entities. They
                  bind to the global state revision, so regional stocks are
                  strictly sequential, and never merge offline. */
               WS_EXCAVATE,
               WS_CONVERT,
               WS_SPEND,
               /* Gate 6: information and exchange vocabulary. SHOW presents
                  an inventory item to an NPC (private, evidence-forming).
                  TELL asserts a claim about a subject to an NPC (claims are
                  recorded as claims, never verified as truth). REPORT files
                  the actor's retained evidence about a subject with a
                  clerk NPC for delayed institutional delivery. EXCHANGE
                  notarizes an exchange with a counterparty (the goods
                  accounting stays with the callers this gate). All four are
                  committed canonical events with retry receipts. */
               WS_SHOW,
               WS_TELL,
               WS_REPORT,
               WS_EXCHANGE } WsAction;
typedef enum { WS_BOUNTY,
               WS_TRADE,
               WS_PLAYER_NOTE,
               WS_WORLD_EVENT,
               WS_RUMOR,
               WS_FACTION_NOTICE,
               WS_DISCOVERY,
               WS_NEWS_RESERVED } WsFeedKind;
typedef struct {
    uint32_t epoch, revision, owner;
    uint16_t quantity, health, decor[4];
    uint8_t alive, public_access, behavior, reserved;
} WsEntity;
typedef struct {
    uint32_t id, sequence, credits;
    uint16_t inventory[WS_INVENTORY];
    uint32_t completed[WS_CAP]; /* Monotone epoch watermark per objective. */
} WsPlayer;
typedef struct {
    int16_t trust, reliability, cooperation, aggression;
    uint16_t confidence, promise;
} WsRelationship;
typedef struct {
    uint32_t revision, actor, epoch;
    uint16_t target, kind;
} WsFeed;
typedef struct {
    uint32_t sequence, epoch, base_revision;
    uint16_t action, target, amount, aux;
} WsOperation;
/* Sparse persistent extraction sites. A PIT is recoverable disturbance: it
   refills from the regional stock while the reservoir recovers. An
   EXCAVATION is intentional topology (foundation, cave entrance) and never
   heals. Sites are accounting records of voids, not matter stocks. */
#define WS_SITE_CAP 32
enum { WS_SITE_PIT = 0, WS_SITE_EXCAVATION = 1 };
typedef struct {
    WsPos pos;
    uint16_t reservoir, kind, extent, reserved;
    uint32_t amount;
} WsSite;
/* Sparse persistent creature exceptions (Gate 5). Generated placement and
   schedules are defaults reconstructed from the product; consequential
   history overrides them per creature, keyed by the stable identity:
   DEAD removes the creature, RELOCATED re-anchors it at a declared walk
   surface, PINNED keeps it at its placed home. Everything else about a
   creature stays generated, so an exception never grows the product. */
#define WS_CREX_CAP 32
enum { WS_CREX_DEAD = 1, WS_CREX_RELOCATED = 2, WS_CREX_PINNED = 3 };
typedef struct {
    WsId id;
    uint16_t kind, aux, reserved, pad;
} WsCreatureEx;
/* Directed evidence records (Gate 6): the retained causal capsules behind
   social projections. Handles are positional: an NPC observer is its entity
   index + 1 (1..WS_CAP); a player handle is 0x10000 + player index. Teller 0
   means direct observation; any other teller marks reported (hearsay)
   evidence whose confidence was degraded at formation and can never rise
   through copying. `root` is the committed revision the record traces to;
   kind is the epistemic class; valence is the authority-assigned contextual
   valence of the referenced act; salience >= WS_EV_PINNED keeps the record
   outside time decay. Uninformed observers simply have no records. */
#define WS_EV_CAP 64
#define WS_EV_PLAYER 0x10000u
#define WS_EV_PINNED 500
enum { WS_EV_ACT = 1, WS_EV_CLAIM, WS_EV_CONTRADICT, WS_EV_RESTITUTION,
       WS_EV_PROMISE, WS_EV_KEPT, WS_EV_BREACH, WS_EV_REPORT };
/* Typed context tags alter interpretation without rewriting history:
   consensual or defensive force counts differently, a shown item is a
   presentation, a promise context marks commitment speech. */
enum { WS_CTX_NONE = 0, WS_CTX_ARENA, WS_CTX_DEFENSE, WS_CTX_RESTITUTION,
       WS_CTX_SHOWN, WS_CTX_PROMISE, WS_CTX_DISPATCH };
typedef struct {
    uint32_t observer, subject, teller, root;
    uint16_t kind, confidence, context;
    uint32_t clock_s;
    uint16_t salience, reserved;
    int16_t valence;
    uint16_t pad;
} WsEvidence;
/* Pending institutional reports: a filed report is delivered to its clerk
   only when the canonical clock passes deliver_s, at the next authority
   touch. Repeated filings of the same (observer, root, teller) replace the
   pending entry instead of stacking. */
#define WS_PEND_CAP 8
/* Institutional dispatch delay in canonical world seconds: a filed report
   reaches its clerk when the clock passes, never instantly. */
#define WS_REPORT_DELAY_S 3600
typedef struct {
    uint32_t observer, subject, teller, root, deliver_s;
    uint16_t kind, confidence, context, salience, reserved;
    int16_t valence;
    uint16_t pad;
} WsPending;
/* Retry receipts: an entity command that committed returns its exact
   disposition to any later replay of the same (player, sequence, op) with no
   repeated cost or reward. Fingerprints mismatching a stored sequence are
   stale sequence reuse, not retries. */
typedef struct {
    uint32_t sequence, epoch, base_revision, revision;
    uint16_t action, target, amount, aux, status;
    uint16_t flags; /* bit 0 rewarded, bit 1 world_changed, bit 2 historical */
} WsReceipt;
/* Deterministic projection of retained evidence for one directed
   observer -> subject pair. Pure function of (records, clock): integer
   decay buckets evaluated against stored anchors, so query frequency,
   save/reload and C/Python targets all answer identically. */
typedef struct {
    int16_t trust; /* bounded -1000..1000 estimate */
    uint16_t acts, claims, contradictions, restitutions, promises, kept, breaches, reports;
    uint16_t confidence; /* total decayed weight, capped 1000 */
    uint32_t watermark;  /* latest committed root seen */
} WsView;
typedef struct {
    WsId ancestry;
    uint32_t recipe_crc, revision;
    uint16_t count, player_count, feed_count, tail_count;
    uint16_t reservoir_count, site_count;
    uint16_t creature_count, creature_pad;
    uint32_t clock_s;
    WsEntity entities[WS_CAP];
    WsPlayer players[WS_PLAYER_CAP];
    WsRelationship relations[WS_PLAYER_CAP][WS_PLAYER_CAP];
    WsFeed feed[WS_FEED_CAP];
    WsOperation tail[WS_TAIL_CAP];
    uint32_t level[WS_RESERVOIR_CAP];
    uint16_t material[WS_PLAYER_CAP], shards[WS_PLAYER_CAP];
    WsSite sites[WS_SITE_CAP];
    WsCreatureEx crex[WS_CREX_CAP];
    uint64_t recovered_total, used_total, lost_total;
    /* Gate 6 social state: evidence capsules, pending dispatches and
       per-player retry receipts. Wire section v4. */
    uint16_t ev_count, pend_count;
    WsEvidence ev[WS_EV_CAP];
    WsPending pend[WS_PEND_CAP];
    WsReceipt receipt[WS_PLAYER_CAP];
} WsState;
typedef struct {
    uint32_t player;
    WsAddress at;
    uint8_t server_authority;
} WsContext;
typedef struct {
    WsError status;
    uint8_t world_changed, rewarded, historical, reserved;
    uint32_t revision;
} WsDisposition;
void ws_state_init(WsState*, const WsRecipe*);
WsError ws_join(WsState*, uint32_t);
WsDisposition ws_apply(WsState*, const WsRecipe*, WsContext, WsOperation);
/* Regional resource layer (resource.c): deterministic weather, rate-based
   recovery with pit healing, kind-aware drawdown, bounded lossy material to
   Phos conversion, and the ledger identity used by ws_state_validate. */
void ws_resources_tick(WsState*, const WsRecipe*, uint32_t delta_s);
int ws_ledger_check(const WsState*, const WsRecipe*);
int ws_river_stage(const WsRecipe*, const WsState*, uint16_t feature, uint16_t t, WsRiverSample*);
/* Nonlocal anomaly gates (link kind WS_LINK_ANOMALY, resource.c): the edge
   is declared in the recipe and anchored by link.reserved to a regional
   Phos reservoir. Whether it may be used is a pure function of canonical
   state, never of special-case coordinates: a gate is open while its
   anchored stock holds at least half the region capacity, so depleting the
   lode removes the shortcut and recharging restores it. With no state
   bound, the declared recipe levels decide (view semantics, like stage).
   ws_route_cost_state mirrors ws_route_cost with closed gates absent;
   ws_use_link_state tries open gates first (lowest link index at a shared
   seat), then the ordinary links unchanged. */
int ws_link_open(const WsRecipe*, const WsState*, uint16_t link);
int ws_route_cost_state(const WsRecipe*, const WsState*, uint16_t a, uint16_t b, uint32_t caps, uint64_t* cost, uint16_t* path, size_t cap);
int ws_use_link_state(const WsRecipe*, const WsState*, WsTraveler*);
/* Routed from ws_apply for action >= WS_EXCAVATE; pi is the actor index. */
WsDisposition ws_resource_apply(WsState*, const WsRecipe*, WsContext, WsOperation, int pi);
/* Sparse creature placement and schedules (creature.c): pure functions of
   (recipe, state exceptions, query time). ws_creature_at resolves one
   creature (0 when it does not exist); ws_creature_query materializes the
   creatures inside a window in (species, slot) order and returns the match
   count, or the negated count when the buffer is too small. State-bound
   only through the exception list; the schedule clock is the caller's. */
int ws_creature_at(const WsRecipe*, const WsState*, uint16_t species, uint16_t slot, uint32_t now_s, WsCreatureSample*);
int ws_creature_query(const WsRecipe*, const WsState*, WsPos lo, WsPos hi, uint32_t now_s, WsCreatureSample* out, int cap);
/* Authority mutation for persistent creature exceptions: kind 0 removes
   the record for id. Fails closed (WS_REFERENCE/WS_FULL/WS_BOUNDS) on
   unknown creatures, bad kinds or a full table; the resulting state must
   still pass ws_state_validate. */
WsError ws_creature_except(WsState*, const WsRecipe*, WsId id, uint16_t kind, uint16_t aux);
/* Is id a creature this recipe can generate (validation helper)? */
int ws_creature_known(const WsRecipe*, WsId);
/* Tail and feed recording shared by the entity and resource op paths.
   feed_kind WS_QUIET records the committed event in the tail only: the act
   is canonical history but not public news (private conversations). After
   the entry, the authority projects the act onto actual NPC witnesses
   through the shared witness gate, so informed observers gain direct
   evidence and everyone else stays unchanged. */
void ws_record(WsState*, const WsRecipe*, WsContext, WsOperation, uint16_t feed_kind);
#define WS_QUIET 0xFFFFu
/* Gate 6 social projections (world/social.c). ws_social_view derives one
   directed observer -> subject estimate from retained evidence; the
   conduct counts, decayed confidence and watermark are the whole answer,
   never a scalar morality. ws_social_cite returns the strongest retained
   evidence handle so dialogue can explain a view through an actual cause,
   or reports the honest empty limit. */
int ws_social_view(const WsState*, uint32_t observer, uint32_t subject, WsView* out);
int ws_social_cite(const WsState*, uint32_t observer, uint32_t subject, uint32_t* root, uint16_t* kind, uint16_t* salience);
/* Authority-side evidence formation for mediated information: the same
   record machinery witness projection uses, available to server scripts
   and NPC-to-NPC flows. root must reference a committed revision. */
WsError ws_inform(WsState*, const WsRecipe*, uint32_t observer, uint32_t subject, uint32_t teller, uint16_t kind, uint16_t confidence, uint16_t context, int16_t valence, uint16_t salience, uint32_t root);
/* Authority-side institutional filing: an observer submits its strongest
   retained evidence about a subject to a clerk NPC for delayed delivery
   (the NPC-to-institution information edge). */
WsError ws_file(WsState*, const WsRecipe*, uint32_t clerk, uint32_t observer, uint32_t subject);
/* Deliver pending institutional reports whose clock has passed, at an
   authority boundary; called from ws_apply, ws_inform and the resource
   tick so delivery order follows canonical commit order. */
void ws_social_settle(WsState*, const WsRecipe*);
/* Advance the canonical world clock for schema-1 worlds without regional
   reservoirs (the resource tick only drives clocked states) and settle
   due dispatches. Bounded per call like the resource tick. */
void ws_clock_advance(WsState*, const WsRecipe*, uint32_t delta_s);
/* Structural handle check shared by evidence formation and validation. */
int ws_ev_handle_ok(const WsState*, const WsRecipe*, uint32_t);
/* Bounded evidence table append with (observer, root, teller, kind)
   replacement semantics; shared by op paths, informs and deliveries. */
void ws_evidence_put(WsState*, const WsEvidence*);
/* base must be a server-retained authenticated branch checkpoint, never a
   client-supplied assertion. Routine merge is deterministic, not AI-mediated. */
WsDisposition ws_merge(WsState*, const WsState*, const WsRecipe*, WsContext, WsOperation);
size_t ws_state_encode(const WsState*, uint8_t*, size_t);
WsError ws_state_decode(WsState*, const WsRecipe*, const uint8_t*, size_t);
WsError ws_state_validate(const WsState*, const WsRecipe*);
uint32_t ws_state_hash(const WsState*);
int ws_save(const WsState*, const char*);
int ws_restore(WsState*, const WsRecipe*, const char*);
#endif
