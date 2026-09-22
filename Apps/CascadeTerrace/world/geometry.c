#include "sdk.h"
#include <stdlib.h>
/* Modules compile to the same bounded boxes used by collision and rendering.
   Room modules have a 4 m south doorway; visual openings are collision openings. */
static void part(WsBoxFn fn, void* ctx, WsPos p, WsPos s, uint32_t c) {
    if (fn) fn(ctx, p, s, c);
}
void ws_boxes(const WsModule* m, WsBoxFn fn, void* ctx) {
    WsPos p = m->pos, s = m->size;
    if ((m->kind & 255) == WS_ROOM_MODULE) {
        WsPos a = s;
        a.y = 300;
        part(fn, ctx, p, a, m->color);
        a = (WsPos) {300, s.y, s.z};
        p.x = m->pos.x - s.x / 2 + 150;
        part(fn, ctx, p, a, m->color);
        p.x = m->pos.x + s.x / 2 - 150;
        part(fn, ctx, p, a, m->color);
        p = m->pos;
        p.z -= s.z / 2 - 150;
        a = (WsPos) {s.x, s.y, 300};
        part(fn, ctx, p, a, m->color);
        a.x = (s.x - 4000) / 2;
        if (a.x > 0) {
            p.z = m->pos.z + s.z / 2 - 150;
            p.x = m->pos.x - (s.x + 4000) / 4;
            part(fn, ctx, p, a, m->color);
            p.x = m->pos.x + (s.x + 4000) / 4;
            part(fn, ctx, p, a, m->color);
        }
    } else if ((m->kind & 255) == WS_RAMP) {
        /* Eight explicit traversable steps; bounded primitive vocabulary. */
        s.z = m->size.z / 8;
        if (s.z < 1) s.z = 1;
        for (int i = 0; i < 8; i++) {
            p.z = m->pos.z - m->size.z / 2 + s.z / 2 + i * s.z;
            s.y = m->size.y * (i + 1) / 8;
            if (!s.y) s.y = 1;
            part(fn, ctx, p, s, m->color);
        }
    } else
        part(fn, ctx, p, s, m->color);
}
typedef struct {
    WsPos p;
    int32_t radius;
    int hit;
} Collision;
static void collide(void* ctx, WsPos p, WsPos s, uint32_t color) {
    (void)color;
    Collision* c = ctx;
    if ((int64_t)c->p.x + c->radius > p.x - s.x / 2 && (int64_t)c->p.x - c->radius < p.x + s.x / 2 &&
        (int64_t)c->p.z + c->radius > p.z - s.z / 2 && (int64_t)c->p.z - c->radius < p.z + s.z / 2 &&
        (int64_t)c->p.y + 1600 > p.y && c->p.y < p.y + s.y - 100) c->hit = 1;
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
            ws_boxes(&m, collide, &c);
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
            if (k > 7) k = 7;
            y += m.size.y * (k + 1) / 8;
        } else
            y += ((m.kind & 255) == WS_ROOM_MODULE ? 300 : m.size.y);
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
