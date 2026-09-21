#include "game.h"
#include "trig.inc"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define INITIAL_TIME (8LL * 3600000)
#define DAY (86400000LL)
static int32_t clamp(int32_t v, int32_t a, int32_t b) { return v < a ? a : v > b ? b
                                                                                 : v; }
static int64_t sq(int64_t x) { return x * x; }
static int32_t distance(Pos a, Pos b) {
    uint64_t n = (uint64_t)(sq(a.x - b.x) + sq(a.z - b.z)), r = 0, bit = 1ULL << 62;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else
            r >>= 1;
        bit >>= 2;
    }
    return (int32_t)r;
}
static int near(Pos a, Pos b, int r) { return sq(a.x - b.x) + sq(a.z - b.z) <= sq(r); }
static int64_t exp_q30(int64_t x) {
    if (x >= 24LL * (1LL << 30)) return 0;
    int n = 0;
    while (x > (1 << 22)) {
        x = (x + 1) / 2;
        n++;
    }
    int64_t q = 1LL << 30, x2 = x * x >> 30, x3 = x2 * x >> 30;
    int64_t y = q - x + x2 / 2 - x3 / 6;
    while (n--) y = y * y >> 30;
    return y;
}
int32_t field_level(const Game* g, int64_t time) {
    const State* s = &g->state;
    int64_t dt = time - s->field_start;
    if (dt < 0) dt = 0;
    int k = 9600 + s->field_k, e = s->repaired ? 4000 : 2400;
    int64_t eq = (int64_t)(9600 - e) * 1000000 / k;
    if (dt > 24LL * 60000 * 1000000 / k) return clamp((int32_t)eq, 0, 1000000);
    int64_t x;
    /* Rearrange to retain sub-minute precision without overflow. */
    x = (dt / 60000) * k * (1LL << 30) / 1000000 + (dt % 60000) * k * (1LL << 20) / 60000 * 1024 / 1000000;
    return clamp((int32_t)(eq + ((s->field_level - eq) * exp_q30(x) >> 30)), 0, 1000000);
}
static void field_close(Game* g) {
    g->state.field_level = field_level(g, g->state.time);
    g->state.field_start = g->state.time;
    g->state.has_field_delta = 1;
}
int market_price(const Game* g) { return g->state.repaired ? 1000 : 1180; }
static Event* record(Game* g, Operation o, Epistemic status, int observed) {
    State* s = &g->state;
    if (s->event_count == EVENT_CAP) {
        memmove(s->events, s->events + 1, (EVENT_CAP - 1) * sizeof(Event));
        s->event_count--;
    }
    Event* e = &s->events[s->event_count++];
    memset(e, 0, sizeof(*e));
    e->id = ++s->event_next;
    e->time = s->time;
    e->actor = o.actor;
    e->target = o.target;
    e->op = o.op;
    e->item = o.item;
    e->amount = o.amount;
    e->status = status;
    if (o.text) snprintf(e->text, sizeof(e->text), "%s", o.text);
    if (observed) {
        NpcMemory* n = &s->npc;
        if (n->count == MEMORY_CAP) {
            memmove(n->memories, n->memories + 1, (MEMORY_CAP - 1) * sizeof(Event));
            n->count--;
        }
        n->memories[n->count++] = *e;
    }
    s->sequence++;
    return e;
}
void game_new(Game* g, uint32_t seed, int variant) {
    memset(g, 0, sizeof(*g));
    generate(&g->world, seed, variant);
    State* s = &g->state;
    s->forced_variant = variant >= 0 ? (uint8_t)(variant % 3) : 255;
    s->seed = seed;
    s->version = GEN_VERSION;
    s->time = INITIAL_TIME;
    s->field_start = s->time;
    s->field_level = 750000;
    s->player_pos = (Pos) {22000, 3000, -90000};
    s->player_pos.y = ground_at(&g->world, s->player_pos.x, s->player_pos.z);
    s->grounded = 1;
    s->yaw = 180;
    s->player.quantity[IT_CHIT] = 0;
    s->kyra.quantity[IT_CHIT] = 34;
    s->kyra.quantity[IT_CONCENTRATOR] = 1;
    s->kyra.quantity[IT_TOOLKIT] = 1;
    s->kyra.quantity[IT_KEY] = 1;
    s->kyra.quantity[IT_NOTE] = 1;
    s->market.quantity[IT_COUPLING] = 1;
    s->market.quantity[IT_CHIT] = 1000;
    s->market.quantity[IT_CONCENTRATOR] = 1;
    s->npc.player = PLAYER_ID;
    snprintf(g->notice, sizeof(g->notice), "Cascade Terrace. Find Kyra beside the station.");
}
void world_advance(Game* g, int64_t ms) {
    if (ms <= 0 || ms > 30 * DAY) return;
    State* s = &g->state;
    s->time += ms;
    /* Core stored in slot 19, milli-Pu. Bounded integer accumulation. */
    s->player.quantity[19] = clamp(s->player.quantity[19] + (int32_t)(ms * 500 / 60000), 0, 100000);
    if (!s->funded && s->time >= INITIAL_TIME + 5 * DAY) {
        s->funded = 1;
        s->kyra.quantity[IT_COUPLING]++;
        record(g, (Operation) {FUND, 3, KYRA_ID, IT_COUPLING, 1, NULL}, MEMORY, 1);
    }
    if (s->promise_state == 1 && s->time > s->promise_deadline) {
        s->promise_state = 3;
        s->npc.trust = clamp(s->npc.trust - 10, -100, 100);
        record(g, (Operation) {BREAK_PROMISE, PLAYER_ID, KYRA_ID, IT_COUPLING, 1, NULL}, MEMORY, 1);
    }
}
int game_apply(Game* g, Operation o) {
    State* s = &g->state;
    Pos p = s->player_pos, npc = npc_position(g), station = g->world.sites[STATION].center;
    station.z += g->world.sites[STATION].halfz;
    if (o.actor != PLAYER_ID) return 0;
    if (o.amount < 0 || o.amount > 1000000) return 0;
    int observed = near(p, npc, 12000) && o.op != WAIT && o.op != EXTRACT && o.op != CONDENSE, a = o.amount ? o.amount : 1;
    Inventory* v = o.target == KYRA_ID ? &s->kyra : &s->market;
    switch (o.op) {
        case WAIT:
            if (o.amount > 43200) return 0;
            world_advance(g, (int64_t)o.amount * 60000);
            break;
        case CONDENSE:
            if (a > 9 || s->player.quantity[19] < a * 11000) return 0;
            s->player.quantity[19] -= a * 11000;
            s->player.quantity[IT_CHIT] += a;
            break;
        case EXTRACT: {
            if (a > 240) return 0;
            int dist = distance(p, (Pos) {0, 0, -125000});
            if (dist >= 30000) return 0;
            field_close(g);
            int coeff = 5000 * (30000 - dist) / 30000;
            if (s->player.quantity[IT_CONCENTRATOR]) coeff = coeff * 3 / 2;
            s->field_k = coeff;
            int32_t before = s->field_level;
            int64_t dt = (int64_t)a * 60000;
            int after = field_level(g, s->time + dt);
            int k = 9600 + coeff;
            int64_t eq = (int64_t)(9600 - (s->repaired ? 4000 : 2400)) * 1000000 / k;
            int64_t integral = eq * a + (int64_t)(before - after) * 1000000 / k;
            int gain = (int)(integral * coeff / 1000000);
            world_advance(g, dt);
            s->player.quantity[19] = clamp(s->player.quantity[19] + gain, 0, 100000);
            field_close(g);
            s->field_k = 0;
            break;
        }
        case REPAIR:
            if (!near(p, station, 2200) || s->repaired || !s->player.quantity[IT_COUPLING]) return 0;
            field_close(g);
            s->player.quantity[IT_COUPLING]--;
            world_advance(g, 120 * 60000LL);
            field_close(g);
            s->repaired = 1;
            observed = near(p, npc_position(g), 15000);
            if (observed) s->npc.trust = clamp(s->npc.trust + 30, -100, 100);
            if (s->promise_state == 1) s->promise_state = 2;
            break;
        case DAMAGE:
            if (!near(p, station, 2200) || !s->repaired) return 0;
            field_close(g);
            s->repaired = 0;
            break;
        case PICK_UP:
            if (o.item != IT_CABLE && o.item != IT_LOG) return 0;
            {
                int i = o.item == IT_CABLE ? 0 : 1;
                if (i == 0 && g->world.variant == 1) return 0;
                if (s->evidence_taken & (1 << i) || !near(p, g->world.evidence[i], 2000)) return 0;
                s->evidence_taken |= 1 << i;
                s->player.quantity[o.item]++;
            }
            break;
        case DROP: /* Evidence keeps a stable authored anchor for now; arbitrary drops rejected. */
            return 0;
        case SHOW:
            if (!near(p, npc, 2200) || (unsigned)o.item >= IT_COUNT || !s->player.quantity[o.item]) return 0;
            if (o.item == IT_CABLE) s->evidence_shown |= 1;
            if (o.item == IT_LOG) s->evidence_shown |= 2;
            observed = 1;
            break;
        case TELL:
        case PROMISE:
            if (!near(p, npc, 2200) || !o.text || strlen(o.text) > 127) return 0;
            observed = 1;
            if (o.op == PROMISE) {
                s->promise_state = 1;
                s->promise_deadline = s->time + DAY;
            }
            break;
        case TRANSFER:
            if (!near(p, npc, 2200) || a > s->player.quantity[IT_CHIT]) return 0;
            s->player.quantity[IT_CHIT] -= a;
            s->kyra.quantity[IT_CHIT] += a;
            observed = 1;
            break;
        case BUY:
        case SELL:
        case GIVE:
        case TAKE: {
            if ((unsigned)o.item >= IT_COUNT) return 0;
            Pos dest = o.target == KYRA_ID ? npc : g->world.sites[MARKET].center;
            if (!near(p, dest, 2500)) return 0;
            if (o.op == TAKE) return 0; /* Consent is not inferred from language. */
            Inventory *from = (o.op == BUY) ? v : &s->player, *to = (o.op == BUY) ? &s->player : v;
            if (from->quantity[o.item] < a) return 0;
            int unit = o.item == IT_COUPLING ? 60 : o.item == IT_CONCENTRATOR ? 45
                                                                              : 1;
            int cost = a * unit;
            if (a > 1000) return 0;
            if (o.op == BUY) {
                if (s->player.quantity[IT_CHIT] < cost) return 0;
                s->player.quantity[IT_CHIT] -= cost;
                v->quantity[IT_CHIT] += cost;
            }
            if (o.op == SELL) {
                if (v->quantity[IT_CHIT] < cost || o.item == IT_CHIT) return 0;
                v->quantity[IT_CHIT] -= cost;
                s->player.quantity[IT_CHIT] += cost;
            }
            from->quantity[o.item] -= a;
            to->quantity[o.item] += a;
            observed = o.target == KYRA_ID;
            if (o.op == GIVE && o.item == IT_COUPLING && s->promise_state == 1) s->promise_state = 2;
            break;
        }
        default:
            return 0;
    }
    record(g, o, o.op == TELL ? CLAIM : o.op == SHOW ? KNOWLEDGE
                                                     : MEMORY,
           observed);
    snprintf(g->notice, sizeof(g->notice), "%s: %s", op_name(o.op), item_name(o.item));
    return 1;
}
void game_tick(Game* g, Input in, uint32_t real_ms) {
    if (real_ms > 250) real_ms = 250;
    g->substep += real_ms;
    State* s = &g->state;
    while (g->substep >= 20) {
        g->substep -= 20;
        world_advance(g, 600);
        s->yaw = (s->yaw + in.turn + 360) % 360;
        int yaw = s->yaw, cs = sine[(yaw + 90) % 360], sn = sine[yaw];
        int speed = in.run ? 120 : 60;
        int32_t dx = (int32_t)((int64_t)(in.forward * sn + in.strafe * cs) * speed / 32767 / 1000), dz = (int32_t)((int64_t)(in.forward * cs - in.strafe * sn) * speed / 32767 / 1000);
        if (in.forward && in.strafe) {
            dx = dx * 707 / 1000;
            dz = dz * 707 / 1000;
        }
        Pos p = s->player_pos;
        for (int axis = 0; axis < 2; axis++) {
            Pos n = p;
            if (axis) n.z += dz;
            else
                n.x += dx;
            int32_t h = ground_at(&g->world, n.x, n.z), old = ground_at(&g->world, p.x, p.z), d = axis ? abs(dz) : abs(dx);
            if (can_stand(g, n, 300) && (h - old <= d + 8 || (!s->grounded && h <= p.y + 400))) {
                p = n;
                if (s->grounded) p.y = h;
            }
        }
        if (in.jump && s->grounded) {
            s->vertical_speed = 4850;
            s->grounded = 0;
        }
        if (!s->grounded) {
            p.y += s->vertical_speed / 50;
            s->vertical_speed -= 196;
            int floor = ground_at(&g->world, p.x, p.z);
            if (p.y <= floor) {
                p.y = floor;
                s->vertical_speed = 0;
                s->grounded = 1;
            }
        }
        s->player_pos = p;
    }
}
