#include "sdk.h"
#include <stdlib.h>

/* Spatial surfaces and access ports. A module's pos.y is the walkable top
   surface; solid geometry hangs below it. Room walls rise from the floor
   slab to pos.y + size.y. Declared walk topology is generated before
   geometry: each walk edge crossing a room wall cuts a doorway port, and
   every room keeps one default public entrance on its south wall. Visual
   openings are collision openings; there are no secret passages. */

static void part(WsBoxFn fn, void* ctx, WsPos p, WsPos s, uint32_t c) {
    if (fn) fn(ctx, p, s, c);
}

/* Truncating division kept identical to the host compiler so the Python
   compiler's port validation is bit-exact with the runtime. */
static int64_t idiv(int64_t a, int64_t b) {
    return a / b;
}

void ws_boxes(const WsModule* m, WsBoxFn fn, void* ctx) {
    WsPos p = m->pos, s = m->size;
    int32_t yc = p.y + (s.y - WS_FLOOR_MM) / 2, hy = s.y + WS_FLOOR_MM;
    if ((m->kind & 255) == WS_ROOM_MODULE) {
        part(fn, ctx, (WsPos) {p.x, p.y - WS_FLOOR_MM / 2, p.z}, (WsPos) {s.x, WS_FLOOR_MM, s.z}, m->color);
        part(fn, ctx, (WsPos) {p.x, yc, p.z - s.z / 2 + 150}, (WsPos) {s.x, hy, 300}, m->color);
        part(fn, ctx, (WsPos) {p.x, yc, p.z + s.z / 2 - 150}, (WsPos) {s.x, hy, 300}, m->color);
        part(fn, ctx, (WsPos) {p.x - s.x / 2 + 150, yc, p.z}, (WsPos) {300, hy, s.z}, m->color);
        part(fn, ctx, (WsPos) {p.x + s.x / 2 - 150, yc, p.z}, (WsPos) {300, hy, s.z}, m->color);
    } else if ((m->kind & 255) == WS_RAMP) {
        /* Eight explicit traversable steps; bounded primitive vocabulary. */
        WsPos st = {s.x, 0, s.z / 8};
        if (st.z < 1) st.z = 1;
        for (int i = 0; i < 8; i++) {
            int32_t h = s.y * (i + 1) / 8;
            if (h < 1) h = 1;
            st.y = h;
            part(fn, ctx, (WsPos) {p.x, p.y + h / 2, p.z - s.z / 2 + st.z / 2 + i * st.z}, st, m->color);
        }
    } else
        part(fn, ctx, (WsPos) {p.x, p.y - s.y / 2, p.z}, s, m->color);
}

typedef struct {
    int wall, exits;
    int32_t along;
} WsCross;

/* Where the segment room-center -> other-center crosses the room boundary.
   At most one wall can be crossed from the centre of a box; a crossing that
   cannot host a legal port fails closed. Returns the exit count (0 or 1) or
   -1 when the declared connection is not realizable. */
static int cross_room(const WsModule* room, WsPos other, WsCross* out) {
    int32_t cx = room->pos.x, cz = room->pos.z;
    int32_t hx = room->size.x / 2, hz = room->size.z / 2;
    out->exits = 0;
    out->wall = WS_WALL_NORTH;
    out->along = cx;
    for (int w = 0; w < 4; w++) {
        int x_plane = w == WS_WALL_EAST || w == WS_WALL_WEST;
        int64_t den = x_plane ? (int64_t)other.x - cx : (int64_t)other.z - cz;
        if (!den) continue;
        int32_t base = x_plane ? cx : cz;
        int32_t plane = w == WS_WALL_EAST ? cx + hx - 150 : w == WS_WALL_WEST ? cx - hx + 150
                                                                               : w == WS_WALL_SOUTH ? cz + hz - 150 : cz - hz + 150;
        int64_t t = idiv((int64_t)(plane - base) * 65536, den);
        if (t <= 0 || t >= 65536) continue;
        int64_t delta = x_plane ? (int64_t)other.z - cz : (int64_t)other.x - cx;
        int64_t rel = idiv(delta * t, 65536);
        int64_t half = x_plane ? hz : hx;
        if (rel > half || rel < -half) continue;
        out->exits++;
        if (out->exits > 1) return -1;
        int64_t limit = half - WS_PORT_MM / 2 - WS_PORT_WALL_MM;
        if (limit < 0 || rel > limit || rel < -limit) return -1;
        out->wall = w;
        out->along = (int32_t)((x_plane ? cz : cx) + rel);
    }
    int outside = llabs((int64_t)other.x - cx) > hx || llabs((int64_t)other.z - cz) > hz;
    if (outside && !out->exits) return -1;
    return out->exits;
}

typedef struct {
    int32_t lo, hi;
} WsSpan;

/* Port spans per wall: the default entrance plus one span per declared walk
   edge crossing. Fails closed (-1) when an edge cannot be realized. */
static int room_spans(const WsRecipe* r, uint16_t index, const WsModule* m, WsSpan wall[4][WS_PORT_MAX], int n[4]) {
    for (int w = 0; w < 4; w++) n[w] = 0;
    if (m->size.x / 2 >= WS_PORT_MM / 2 + WS_PORT_WALL_MM) {
        wall[WS_WALL_SOUTH][0].lo = m->pos.x - WS_PORT_MM / 2;
        wall[WS_WALL_SOUTH][0].hi = m->pos.x + WS_PORT_MM / 2;
        n[WS_WALL_SOUTH] = 1;
    }
    for (uint16_t i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->kind != WS_PORT_WALK) continue;
        uint16_t other = l->a == index ? l->b : l->b == index ? l->a : UINT16_MAX;
        if (other == UINT16_MAX) continue;
        WsModule o;
        ws_materialize(r, other, &o);
        WsCross c;
        if (cross_room(m, o.pos, &c) < 0) return -1;
        if (c.exits && n[c.wall] < WS_PORT_MAX) {
            wall[c.wall][n[c.wall]].lo = c.along - WS_PORT_MM / 2;
            wall[c.wall][n[c.wall]].hi = c.along + WS_PORT_MM / 2;
            n[c.wall]++;
        }
    }
    for (int w = 0; w < 4; w++) {
        for (int i = 1; i < n[w]; i++) {
            WsSpan s = wall[w][i];
            int j = i - 1;
            while (j >= 0 && wall[w][j].lo > s.lo) {
                wall[w][j + 1] = wall[w][j];
                j--;
            }
            wall[w][j + 1] = s;
        }
        int k = 0;
        for (int i = 0; i < n[w]; i++)
            if (k && wall[w][i].lo <= wall[w][k - 1].hi) {
                if (wall[w][i].hi > wall[w][k - 1].hi) wall[w][k - 1].hi = wall[w][i].hi;
            } else
                wall[w][k++] = wall[w][i];
        n[w] = k;
    }
    return 0;
}

static void emit_wall(WsBoxFn fn, void* ctx, const WsModule* m, int w, const WsSpan* span, int n) {
    int x_plane = w == WS_WALL_EAST || w == WS_WALL_WEST;
    int32_t hx = m->size.x / 2, hz = m->size.z / 2;
    int32_t yc = m->pos.y + (m->size.y - WS_FLOOR_MM) / 2, hy = m->size.y + WS_FLOOR_MM;
    int32_t plane = w == WS_WALL_EAST ? m->pos.x + hx - 150 : w == WS_WALL_WEST ? m->pos.x - hx + 150
                                                                                : w == WS_WALL_SOUTH ? m->pos.z + hz - 150 : m->pos.z - hz + 150;
    int32_t e0 = x_plane ? m->pos.z - hz : m->pos.x - hx;
    int32_t e1 = x_plane ? m->pos.z + hz : m->pos.x + hx;
    int32_t edge[2 * (WS_PORT_MAX + 1)];
    int k = 0;
    edge[k++] = e0;
    for (int i = 0; i < n; i++) {
        edge[k++] = span[i].lo;
        edge[k++] = span[i].hi;
    }
    edge[k++] = e1;
    for (int j = 0; j + 1 < k; j += 2) {
        int32_t lo = edge[j], hi = edge[j + 1];
        if (hi - lo <= 0) continue;
        if (x_plane)
            part(fn, ctx, (WsPos) {plane, yc, (lo + hi) / 2}, (WsPos) {300, hy, hi - lo}, m->color);
        else
            part(fn, ctx, (WsPos) {(lo + hi) / 2, yc, plane}, (WsPos) {hi - lo, hy, 300}, m->color);
    }
}

void ws_geometry(const WsRecipe* r, uint16_t index, WsBoxFn fn, void* ctx) {
    if (index >= r->count) return;
    WsModule m;
    ws_materialize(r, index, &m);
    if ((m.kind & 255) != WS_ROOM_MODULE) {
        ws_boxes(&m, fn, ctx);
        return;
    }
    WsSpan wall[4][WS_PORT_MAX];
    int n[4];
    if (room_spans(r, index, &m, wall, n)) {
        /* Products that escaped validation fail closed: unported primitive. */
        ws_boxes(&m, fn, ctx);
        return;
    }
    part(fn, ctx, (WsPos) {m.pos.x, m.pos.y - WS_FLOOR_MM / 2, m.pos.z}, (WsPos) {m.size.x, WS_FLOOR_MM, m.size.z}, m.color);
    for (int w = 0; w < 4; w++) emit_wall(fn, ctx, &m, w, wall[w], n[w]);
}

int ws_ports(const WsRecipe* r, uint16_t index, WsPort* out, int cap) {
    if (index >= r->count) return 0;
    WsModule m;
    ws_materialize(r, index, &m);
    if ((m.kind & 255) != WS_ROOM_MODULE) return 0;
    int total = 0;
    int32_t hz = m.size.z / 2;
    if (m.size.x / 2 >= WS_PORT_MM / 2 + WS_PORT_WALL_MM) {
        WsPort p = {{m.pos.x, m.pos.y, m.pos.z + hz - 150}, index, WS_WALL_SOUTH, WS_PORT_WALK, WS_PORT_MM};
        if (out && total < cap) out[total] = p;
        total++;
    }
    for (uint16_t i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->kind != WS_PORT_WALK) continue;
        uint16_t other = l->a == index ? l->b : l->b == index ? l->a : UINT16_MAX;
        if (other == UINT16_MAX) continue;
        WsModule o;
        ws_materialize(r, other, &o);
        WsCross c;
        if (cross_room(&m, o.pos, &c) < 0) continue;
        if (!c.exits) continue;
        WsPort p = {{0, m.pos.y, 0}, index, (uint16_t)c.wall, WS_PORT_WALK, WS_PORT_MM};
        if (c.wall == WS_WALL_EAST || c.wall == WS_WALL_WEST) {
            p.pos.x = c.wall == WS_WALL_EAST ? m.pos.x + m.size.x / 2 - 150 : m.pos.x - m.size.x / 2 + 150;
            p.pos.z = c.along;
        } else {
            p.pos.z = c.wall == WS_WALL_SOUTH ? m.pos.z + hz - 150 : m.pos.z - hz + 150;
            p.pos.x = c.along;
        }
        if (out && total < cap) out[total] = p;
        total++;
    }
    return total;
}

typedef struct {
    WsPos p;
    int32_t radius;
    int hit;
} Collision;

static void collide(void* ctx, WsPos p, WsPos s, uint32_t color) {
    (void)color;
    Collision* c = ctx;
    /* Walker collides with the box's real volume: up to 1600 mm of body
       below its feet may overlap the box, and 100 mm of sole clearance
       keeps a walker standing on the surface out of the volume. */
    if ((int64_t)c->p.x + c->radius > p.x - s.x / 2 && (int64_t)c->p.x - c->radius < p.x + s.x / 2 &&
        (int64_t)c->p.z + c->radius > p.z - s.z / 2 && (int64_t)c->p.z - c->radius < p.z + s.z / 2 &&
        (int64_t)c->p.y + 1600 > p.y - s.y / 2 && c->p.y < p.y + s.y / 2 - 100) c->hit = 1;
}

int ws_collision(const WsRecipe* r, WsAddress at, int32_t radius) {
    if (radius < 0 || radius > 2000) return 1;
    Collision c = {at.pos, radius, 0};
    for (uint16_t i = 0; i < r->count; i++) {
        const WsModule* src = &r->modules[i];
        int interior = (src->flags & WS_INTERIOR) != 0;
        if ((at.scope == UINT16_MAX && interior) || (at.scope != UINT16_MAX && i != at.scope && src->parent != at.scope)) continue;
        if (src->flags & WS_SOLID) {
            WsModule m;
            ws_materialize(r, i, &m);
            if (llabs((int64_t)c.p.x - m.pos.x) > m.size.x / 2 + radius + 300 || llabs((int64_t)c.p.z - m.pos.z) > m.size.z / 2 + radius + 300) continue;
            ws_geometry(r, i, collide, &c);
        }
    }
    return c.hit;
}

int ws_surface(const WsRecipe* r, WsAddress at, int32_t ceiling, int32_t* height) {
    int found = 0;
    int32_t best = INT32_MIN;
    for (uint16_t i = 0; i < r->count; i++) {
        const WsModule* src = &r->modules[i];
        if (!(src->flags & WS_WALK)) continue;
        if ((at.scope == UINT16_MAX && (src->flags & WS_INTERIOR)) || (at.scope != UINT16_MAX && i != at.scope && src->parent != at.scope)) continue;
        WsModule m;
        ws_materialize(r, i, &m);
        if (llabs((int64_t)at.pos.x - m.pos.x) > m.size.x / 2 || llabs((int64_t)at.pos.z - m.pos.z) > m.size.z / 2) continue;
        int32_t y = m.pos.y;
        if ((m.kind & 255) == WS_RAMP) {
            int64_t t = (int64_t)at.pos.z - m.pos.z + m.size.z / 2;
            int k = (int)(t * 8 / m.size.z);
            if (k < 0) k = 0;
            if (k > 7) k = 7;
            y += m.size.y * (k + 1) / 8;
        }
        if (y <= ceiling && y > best) {
            best = y;
            found = 1;
        }
    }
    if (found) *height = best;
    return found;
}

/* Line samples are bounded to 128; wall occlusion uses shared collision geometry.
   This is a coarse visibility gate, not a continuous ray tracer. */
uint16_t ws_witness(const WsRecipe* r, WsAddress observer, WsAddress event, uint16_t attention, uint16_t conspicuous) {
    if (observer.scope != event.scope || attention > 1000 || conspicuous > 1000) return 0;
    int64_t dx = (int64_t)event.pos.x - observer.pos.x, dy = (int64_t)event.pos.y - observer.pos.y, dz = (int64_t)event.pos.z - observer.pos.z;
    if (llabs(dx) > 30000 || llabs(dy) > 30000 || llabs(dz) > 30000) return 0;
    int64_t d = llabs(dx) + llabs(dy) + llabs(dz);
    if (d >= 30000) return 0;
    for (int i = 1; i < 128; i++) {
        WsAddress p = observer;
        p.pos = (WsPos) {observer.pos.x + (int32_t)(dx * i / 128), observer.pos.y + (int32_t)(dy * i / 128), observer.pos.z + (int32_t)(dz * i / 128)};
        if (ws_collision(r, p, 0)) return 0;
    }
    return (uint16_t)((30000 - d) * attention * conspicuous / 30000000LL);
}

/* Would the direct route between two walk surfaces pass through an unrelated
   solid structure? Uses the walker's own collision window and radius. */
static int route_blocked(WsPos a, WsPos b, const WsModule* t) {
    int room = (t->kind & 255) == WS_ROOM_MODULE;
    int ramp = (t->kind & 255) == WS_RAMP;
    int32_t y0 = ramp ? t->pos.y : room ? t->pos.y - WS_FLOOR_MM : t->pos.y - t->size.y;
    int32_t y1 = ramp ? t->pos.y + t->size.y : room ? t->pos.y + t->size.y : t->pos.y;
    int64_t lo = 1, hi = 65535;
    int64_t d[3] = {(int64_t)b.x - a.x, (int64_t)b.y - a.y, (int64_t)b.z - a.z};
    int64_t p[3] = {a.x, a.y, a.z};
    int64_t lo_b[3] = {t->pos.x - t->size.x / 2 - 300, y0 - 1600, t->pos.z - t->size.z / 2 - 300};
    int64_t hi_b[3] = {t->pos.x + t->size.x / 2 + 300, y1 - 100, t->pos.z + t->size.z / 2 + 300};
    for (int i = 0; i < 3; i++) {
        if (!d[i]) {
            if (p[i] <= lo_b[i] || p[i] >= hi_b[i]) return 0;
        } else {
            int64_t tl = idiv((lo_b[i] - p[i]) * 65536, d[i]);
            int64_t th = idiv((hi_b[i] - p[i]) * 65536, d[i]);
            if (tl > th) {
                int64_t s = tl;
                tl = th;
                th = s;
            }
            if (tl > lo) lo = tl;
            if (th < hi) hi = th;
            if (lo >= hi) return 0;
        }
    }
    return 1;
}

WsError ws_topology(const WsRecipe* r) {
    for (int i = 0; i < r->link_count; i++) {
        const WsLink* l = &r->links[i];
        if (l->kind != WS_PORT_WALK) continue;
        const WsModule* ma = &r->modules[l->a];
        const WsModule* mb = &r->modules[l->b];
        int fa = (ma->flags & WS_INTERIOR) != 0, fb = (mb->flags & WS_INTERIOR) != 0;
        /* Walk edges stay within one bounded local coordinate frame. */
        if (fa != fb || (fa && ma->parent != mb->parent)) return WS_REFERENCE;
        WsModule A, B;
        ws_materialize(r, l->a, &A);
        ws_materialize(r, l->b, &B);
        /* Room endpoints need a legal port where the edge crosses a wall. */
        WsCross cross;
        if ((A.kind & 255) == WS_ROOM_MODULE && cross_room(&A, B.pos, &cross) < 0) return WS_BOUNDS;
        if ((B.kind & 255) == WS_ROOM_MODULE && cross_room(&B, A.pos, &cross) < 0) return WS_BOUNDS;
        /* Baseline walk keeps steps enterable and slopes walkable. */
        if ((A.kind & 255) == WS_RAMP && A.size.y > 800) return WS_BOUNDS;
        if ((B.kind & 255) == WS_RAMP && B.size.y > 800) return WS_BOUNDS;
        int64_t dx = (int64_t)B.pos.x - A.pos.x, dy = (int64_t)B.pos.y - A.pos.y, dz = (int64_t)B.pos.z - A.pos.z;
        if (dy * dy > dx * dx + dz * dz) return WS_BOUNDS;
        /* The direct route may not pass through unrelated solid structure. */
        for (int j = 0; j < r->count; j++) {
            if (j == l->a || j == l->b) continue;
            const WsModule* t = &r->modules[j];
            if (!(t->flags & WS_SOLID)) continue;
            int fj = (t->flags & WS_INTERIOR) != 0;
            if (fj != fa) continue;
            if (fa && t->parent != ma->parent) continue;
            WsModule T;
            ws_materialize(r, (uint16_t)j, &T);
            if (route_blocked(A.pos, B.pos, &T)) return WS_BOUNDS;
        }
    }
    /* Baseline public access: an NPC (vendor) may not be sealed inside a
       room with no access ports. Content may hide secrets behind future
       abilities; it may not seal a mandatory vendor. */
    for (int i = 0; i < r->count; i++) {
        const WsModule* m = &r->modules[i];
        if (!(m->flags & WS_NPC)) continue;
        uint16_t room = (m->kind & 255) == WS_ROOM_MODULE ? (uint16_t)i : m->parent;
        if (room != UINT16_MAX && room < r->count && (r->modules[room].kind & 255) == WS_ROOM_MODULE && !ws_ports(r, room, 0, 0))
            return WS_DISCONNECTED;
    }
    return WS_OK;
}

WsReach ws_reachable(const WsRecipe* r, uint16_t a, uint16_t b, uint32_t caps) {
    if (a >= r->count || b >= r->count) return WS_REACH_INVALID;
    if (!(r->modules[a].flags & WS_WALK) || !(r->modules[b].flags & WS_WALK)) return WS_REACH_INVALID;
    if (a == b) return WS_REACH_WALK;
    /* Pass 0: baseline walk edges only. Pass 1: all edges the declared
       capability set covers. Pass 2: all edges, to tell a missing capability
       from a genuinely unreachable surface. */
    for (int pass = 0; pass < 3; pass++) {
        uint8_t seen[WS_CAP] = {0};
        uint16_t queue[WS_CAP];
        size_t head = 0, tail = 0;
        queue[tail++] = a;
        seen[a] = 1;
        while (head < tail) {
            uint16_t at = queue[head++];
            if (at == b) return pass == 0 ? WS_REACH_WALK : pass == 1 ? WS_REACH_ABILITY : WS_REACH_CONDITIONAL;
            for (int i = 0; i < r->link_count; i++) {
                const WsLink* l = &r->links[i];
                uint16_t next = l->a == at ? l->b : l->b == at ? l->a : UINT16_MAX;
                if (next >= r->count || seen[next]) continue;
                if (pass == 0 && l->kind) continue;
                if (pass == 1) {
                    uint32_t need = l->kind == 1 ? WS_CAP_LIFT : l->kind == 2 ? WS_CAP_PORTAL : WS_CAP_WALK;
                    if ((caps & need) != need) continue;
                }
                seen[next] = 1;
                queue[tail++] = next;
            }
        }
    }
    return WS_REACH_INACCESSIBLE;
}
