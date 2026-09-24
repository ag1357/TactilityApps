#!/usr/bin/env python3
"""Bounded World SDK source compiler. Standard library only. No runtime JSON."""

import argparse, hashlib, json, math, pathlib, struct, sys, zlib

SCHEMA = GENERATOR = 1
SCHEMA_FEATURES = 2
SCHEMA_RESOURCES = 3
SCHEMA_CREATURES = 4
TAGS = {
    "Surface.Walk": 1,
    "Collision.Solid": 2,
    "Action.Repair": 4,
    "Property.Ownable": 8,
    "Entity.NPC": 16,
    "Phos.Resource": 32,
    "Scope.Interior": 64,
}
PHOS = {"Neutral": 0, "Hydro": 1, "Elek": 2, "Sol": 3, "Geo": 4, "Bio": 5, "Aero": 6}
SHAPES = {"box": 0, "plane": 1, "ramp": 2, "room": 3}
LEVELS = [
    "world",
    "region",
    "settlement",
    "district",
    "structure",
    "floor",
    "room",
    "object",
]
FEATURE_KINDS = {"river": 1}
FLOWS = {"calm": 0, "rapid": 1}
EXCEPTIONS = {"waterfall": 1, "rapids": 2, "lake": 3, "dam": 4, "underground": 5}
RESERVOIR_KINDS = {"terrain": 1, "water": 2, "biomass": 3, "phos": 4}
CREATURE_KINDS = {"fauna": 1, "npc": 2, "required": 3}
CREX_DEAD, CREX_RELOCATED, CREX_PINNED = 1, 2, 3
CRE_DWELL, CRE_SPEED, CRE_TRIES, CRE_HOPS = 64, 4096, 16, 2
WEATHER_MULT = ((1, 1, 1), (1, 2, 3), (1, 2, 1), (1, 1, 2))
CAP_WALK, CAP_LIFT, CAP_PORTAL, CAP_ANOMALY = 1, 2, 4, 8
LINK_KINDS = {"walk": 0, "lift": 1, "portal": 2, "anomaly": 3}
ANOMALY_COST = 1500


def h(x):
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = x * 0x7FEB352D & 0xFFFFFFFF
    x ^= x >> 15
    x = x * 0x846CA68B & 0xFFFFFFFF
    return x ^ (x >> 16)


def c_div(a, b):
    """Truncating division matching C, for bit-exact runtime parity."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def materialized(src, m):
    """Runtime materialization: correlated seed jitter over source position."""
    j = h(src["seed"] ^ m.get("variation_seed", m["key"])) % 5 - 2
    jx, jz = m.get("jitter", [0, 0])
    p = list(m["position"])
    p[0] += j * jx
    p[2] += j * jz
    return p


def cross_room(rpos, rsize, other):
    """Mirror of the runtime wall-crossing check: where the direct center
    segment leaves a room, a legal access port must fit (fail closed)."""
    cx, cz = rpos[0], rpos[2]
    hx, hz = rsize[0] // 2, rsize[2] // 2
    exits = []
    for wall, x_plane, plane in (
        ("north", False, cz - hz + 150),
        ("east", True, cx + hx - 150),
        ("south", False, cz + hz - 150),
        ("west", True, cx - hx + 150),
    ):
        base, den = (cx, other[0] - cx) if x_plane else (cz, other[2] - cz)
        if not den:
            continue
        t = c_div((plane - base) << 16, den)
        if t <= 0 or t >= 65536:
            continue
        rel = c_div(((other[2] - cz) if x_plane else (other[0] - cx)) * t, 65536)
        half = hz if x_plane else hx
        if rel > half or rel < -half:
            continue
        limit = half - 2300
        if limit < 0 or rel > limit or rel < -limit:
            raise ValueError(f"walk link crossing cannot host a 4000 mm port on the {wall} wall")
        exits.append(wall)
    if len(exits) > 1:
        raise ValueError("walk link crosses more than one room wall")
    if (abs(other[0] - cx) > hx or abs(other[2] - cz) > hz) and not exits:
        raise ValueError("walk link leaves the room without crossing a wall")
    return exits


def route_blocked(a, b, t, tm):
    """Mirror of the runtime direct-route blockage test against an unrelated
    solid structure, using the walker's own collision window and radius."""
    room = t["shape"] == "room"
    ramp = t["shape"] == "ramp"
    ty, tsy = tm[1], t["size"][1]
    y0 = ty if ramp else (ty - 300 if room else ty - tsy)
    y1 = ty + tsy if (room or ramp) else ty
    hx, hz = t["size"][0] // 2 + 300, t["size"][2] // 2 + 300
    lo, hi = 1, 65535
    for b0, b1, p, d in (
        (tm[0] - hx, tm[0] + hx, a[0], b[0] - a[0]),
        (y0 - 1600, y1 - 100, a[1], b[1] - a[1]),
        (tm[2] - hz, tm[2] + hz, a[2], b[2] - a[2]),
    ):
        if not d:
            if p <= b0 or p >= b1:
                return False
        else:
            tl, th = c_div((b0 - p) << 16, d), c_div((b1 - p) << 16, d)
            if tl > th:
                tl, th = th, tl
            lo, hi = max(lo, tl), min(hi, th)
            if lo >= hi:
                return False
    return True


def validate_walk_edge(src, modules, a, b):
    """Walk edges are declared baseline-walkable topology: one local frame,
    legal ports, no unrelated solid structure on the direct route, walkable
    slopes and enterable ramp steps. CONTENT that cannot satisfy this is
    rejected; it is not silently reinterpreted."""
    ma, mb = modules[a], modules[b]
    fa = "Scope.Interior" in ma.get("tags", [])
    fb = "Scope.Interior" in mb.get("tags", [])
    if fa != fb or (fa and ma.get("parent") != mb.get("parent")):
        raise ValueError("walk link spans different local coordinate frames")
    A, B = materialized(src, ma), materialized(src, mb)
    for room, other in ((ma, B), (mb, A)):
        if room["shape"] == "room":
            cross_room(materialized(src, room), room["size"], other)
    for m in (ma, mb):
        if m["shape"] == "ramp" and m["size"][1] > 800:
            raise ValueError("ramp steps exceed baseline walk entry")
    dx, dy, dz = B[0] - A[0], B[1] - A[1], B[2] - A[2]
    if dy * dy > dx * dx + dz * dz:
        raise ValueError("walk link slope exceeds baseline walk")
    for j, t in enumerate(modules):
        if j in (a, b) or "Collision.Solid" not in t.get("tags", []):
            continue
        fj = "Scope.Interior" in t.get("tags", [])
        if fj != fa:
            continue
        if fa and t.get("parent") != ma.get("parent"):
            continue
        if route_blocked(A, B, t, materialized(src, t)):
            raise ValueError("walk link crosses unrelated solid structure; declare the intermediate edges")


def child(parent, key):
    return [
        h(p ^ h(key + 0x9E3779B9 * (i + 1)) ^ GENERATOR) for i, p in enumerate(parent)
    ]


def bounded(v, lo, hi, label):
    if type(v) != int or not lo <= v <= hi:
        raise ValueError(f"{label}: integer {lo}..{hi} required")
    return v


def fraction(v, label):
    if type(v) not in (int, float) or not 0 <= v < 1:
        raise ValueError(f"{label}: fraction 0..1 required")
    return v


def feature_record(f):
    """Validate and convert one source feature into the runtime record. The
    same conversion backs both the packing path and the ws_river_sample
    mirror, so there is exactly one definition of at/length/drop semantics."""
    if set(f) - {
        "name",
        "key",
        "kind",
        "flow",
        "upstream",
        "downstream",
        "width",
        "depth",
        "seed",
        "exceptions",
    }:
        raise ValueError("unknown feature field")
    kind = FEATURE_KINDS.get(f.get("kind", "river"))
    if kind is None:
        raise ValueError(f"unknown feature kind: {f.get('kind')}")
    flow = FLOWS.get(f.get("flow", "calm"))
    if flow is None:
        raise ValueError(f"unknown flow class: {f.get('flow')}")
    up = [bounded(v, -1000000, 1000000, "upstream") for v in f["upstream"]]
    down = [bounded(v, -1000000, 1000000, "downstream") for v in f["downstream"]]
    if len(up) != 3 or len(down) != 3:
        raise ValueError("feature coordinate dimensions")
    width = bounded(f["width"], 200, 32000, "width")
    depth = bounded(f["depth"], 50, 8000, "depth")
    dx, dz = down[0] - up[0], down[2] - up[2]
    if dx == 0 and dz == 0:
        raise ValueError("river has no horizontal run")
    rec = {
        "kind": kind,
        "flow": flow,
        "up": up,
        "down": down,
        "width": width,
        "depth": depth,
        "seed": bounded(f.get("seed", 0), 0, 0xFFFFFFFF, "feature seed"),
        "exceptions": [],
    }
    drop = up[1] - down[1]
    steps = 0
    for e in f.get("exceptions", []):
        if set(e) - {"type", "at", "length", "drop"}:
            raise ValueError("unknown exception field")
        t = EXCEPTIONS.get(e["type"])
        if t is None:
            raise ValueError(f"unknown exception type: {e['type']}")
        at = int(fraction(e["at"], "exception at") * 65535)
        length = int(fraction(e.get("length", 0), "exception length") * 65535)
        if at + length > 65535:
            raise ValueError("exception overruns the feature")
        aux = 0
        if t in (EXCEPTIONS["waterfall"], EXCEPTIONS["dam"]):
            aux = bounded(e["drop"], 1, 200000, "exception drop")
            if t == EXCEPTIONS["waterfall"] and length:
                raise ValueError("waterfall is a point exception")
            if at + length > 65534:
                raise ValueError("drop must complete before the downstream end")
            steps += aux
        else:
            if "drop" in e:
                raise ValueError(f"{e['type']} has no drop")
            if length < 1:
                raise ValueError(f"{e['type']} needs a reach length")
        rec["exceptions"].append({"type": t, "at": at, "length": length, "aux": aux})
    if drop < 1:
        raise ValueError("river must fall monotonically (upstream above downstream)")
    if steps > drop:
        raise ValueError("typed drops exceed the total fall")
    if (drop - steps) * 4 > math.isqrt(dx * dx + dz * dz):
        raise ValueError("river slope exceeds the bounded grade after typed drops")
    return rec


def reservoir_record(v):
    """Validate and convert one source reservoir into the runtime record:
    a bounded regional stock with rate-based recovery parameters. The same
    conversion backs the packing path and the ws_resources_tick mirror."""
    if set(v) - {"name", "key", "kind", "zone", "lo", "hi", "capacity", "level", "rate"}:
        raise ValueError("unknown reservoir field")
    kind = RESERVOIR_KINDS.get(v.get("kind"))
    if kind is None:
        raise ValueError(f"unknown reservoir kind: {v.get('kind')}")
    zone = bounded(v.get("zone", 0), 0, 3, "zone")
    lo = [bounded(c, -1000000, 1000000, "lo") for c in v["lo"]]
    hi = [bounded(c, -1000000, 1000000, "hi") for c in v["hi"]]
    if len(lo) != 3 or len(hi) != 3:
        raise ValueError("reservoir coordinate dimensions")
    if any(a >= b for a, b in zip(lo, hi)):
        raise ValueError("reservoir region must be non-degenerate on every axis")
    capacity = bounded(v["capacity"], 1, 400000, "capacity")
    level = bounded(v.get("level", capacity), 0, capacity, "level")
    rate = bounded(v["rate"], 1, 65535, "rate")
    return {"kind": kind, "zone": zone, "lo": lo, "hi": hi, "capacity": capacity, "level": level, "rate": rate}


def creature_record(v, module_ids, reservoir_names):
    """Validate and convert one source creature species into the runtime
    record: a bounded generator description, never an entity list. The
    habitat names a biome reservoir (fauna) or an anchor module (npc and
    required); both resolve to product indices here, fail-closed."""
    if set(v) - {"name", "key", "kind", "habitat", "slots", "period", "stations", "radius", "seed"}:
        raise ValueError("unknown creature field")
    kind = CREATURE_KINDS.get(v.get("kind"))
    if kind is None:
        raise ValueError(f"unknown creature kind: {v.get('kind')}")
    habitat = v["habitat"]
    if kind == CREATURE_KINDS["fauna"]:
        if habitat not in reservoir_names:
            raise ValueError("fauna habitat must be a declared reservoir")
        hindex = reservoir_names[habitat]
    else:
        if habitat not in module_ids:
            raise ValueError("anchored species need a declared anchor module")
        hindex = module_ids[habitat]
    slots = bounded(v["slots"], 1, 64, "slots")
    period = bounded(v["period"], 600, 64800, "period")
    stations = bounded(v["stations"], 1, 4, "stations")
    radius = bounded(v["radius"], 2000, 60000, "radius")
    seed = bounded(v.get("seed", 0), 0, 0xFFFFFFFF, "creature seed")
    return {
        "kind": kind,
        "habitat": hindex,
        "slots": slots,
        "period": period,
        "stations": stations,
        "radius": radius,
        "seed": seed,
    }


def creature_view(src):
    """Resolve a source recipe into the creature runtime numbers C sees:
    materialized modules with tags, resolved walk links, compiled feature
    and reservoir records, and compiled creature species records with
    stable identities. One structure backs every creature mirror below."""
    ids = {m["name"]: i for i, m in enumerate(src["modules"])}
    modules = [dict(m, at=materialized(src, m)) for m in src["modules"]]
    positions = [m["at"] for m in modules]
    links = []
    for link in src.get("links", []):
        a, b, kind = link[0], link[1], link[2]
        links.append((ids[a], ids[b], LINK_KINDS[kind], None))
    features = [feature_record(f) for f in src.get("features", [])]
    reservoirs = [reservoir_record(v) for v in src.get("reservoirs", [])]
    ancestry = [bounded(a, 0, 0xFFFFFFFF, "ancestry") for a in src["ancestry"]]
    rnames = {v["name"]: i for i, v in enumerate(src.get("reservoirs", []))}
    creatures = []
    for i, c in enumerate(src.get("creatures", [])):
        rec = creature_record(c, ids, rnames)
        rec["id"] = child(ancestry, bounded(c.get("key", 300 + i), 1, 0xFFFFFFFF, "creature key"))
        rec["index"] = len(creatures)
        creatures.append(rec)
    return {
        "ids": ids,
        "modules": modules,
        "positions": positions,
        "links": links,
        "features": features,
        "reservoirs": reservoirs,
        "creatures": creatures,
    }


def creature_id(view, species, slot):
    """Mirror of ws_creature_id: stable identity from the species identity
    and the slot number (never coordinates or transient handles)."""
    return child_id(view["creatures"][species]["id"], slot + 1, 0x4352)


def child_id(parent, key, version):
    """Mirror of ws_child_id with an explicit version word."""
    return [
        h(parent[i] ^ h((key + 0x9E3779B9 * (i + 1)) & 0xFFFFFFFF) ^ version)
        for i in range(4)
    ]


def _cre_blocked(view, p):
    """Mirror of the placement blocker test: strictly inside a solid box,
    or inside a room's wall span (rooms are interiors; exterior placement
    stays out). Standing exactly on a box top is free."""
    for m in view["modules"]:
        shape = SHAPES[m["shape"]]
        at, size = m["at"], m["size"]
        if shape == 0 and "Collision.Solid" in m.get("tags", []):
            if (
                at[0] - size[0] // 2 < p[0] < at[0] + size[0] // 2
                and at[2] - size[2] // 2 < p[2] < at[2] + size[2] // 2
                and at[1] - size[1] < p[1] < at[1]
            ):
                return True
        if shape == 3:
            if (
                at[0] - size[0] // 2 < p[0] < at[0] + size[0] // 2
                and at[2] - size[2] // 2 < p[2] < at[2] + size[2] // 2
                and at[1] - 300 < p[1] < at[1] + size[1]
            ):
                return True
    return False


def _cre_terrain_top(view, x, z, floor_y):
    """Mirror of terrain_top: the highest solid box top under (x, z), or
    the region floor where no solid box contains the point."""
    top = None
    for m in view["modules"]:
        if SHAPES[m["shape"]] != 0 or "Collision.Solid" not in m.get("tags", []):
            continue
        at, size = m["at"], m["size"]
        if not (at[0] - size[0] // 2 <= x <= at[0] + size[0] // 2):
            continue
        if not (at[2] - size[2] // 2 <= z <= at[2] + size[2] // 2):
            continue
        if top is None or at[1] > top:
            top = at[1]
    return floor_y if top is None else top


def _cre_spread(salt, key, radius):
    return (h(salt ^ key) % (2 * radius + 1)) - radius


def _cre_chord(a, b):
    n = (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2 + (b[2] - a[2]) ** 2
    return math.isqrt(n) if n > 0 else 0


def _cre_point_at(pts, d):
    """Mirror of point_at: position at distance d along the polyline with
    16.16 per-component interpolation, truncating toward zero (c_div)."""
    for i in range(len(pts) - 1):
        lc = _cre_chord(pts[i], pts[i + 1])
        if lc <= 0:
            continue
        if d < lc:
            t = d * 65536 // lc
            return [
                pts[i][k] + c_div((pts[i + 1][k] - pts[i][k]) * t, 65536)
                for k in range(3)
            ]
        d -= lc
    return list(pts[-1])


def _cre_home(view, c, slot, anchored, anchor):
    s1 = h(c["seed"] ^ (((slot + 1) * 0x9E3779B9) & 0xFFFFFFFF))
    if anchored:
        a = view["modules"][anchor]
        ax, ay, az = a["at"]
        hx, hz = a["size"][0] // 2 - 600, a["size"][2] // 2 - 600
        for i in range(CRE_TRIES):
            p = [
                ax + _cre_spread(s1, (0xA1 + i * 0x85EBCA6B) & 0xFFFFFFFF, c["radius"]),
                ay,
                az + _cre_spread(s1, (0xC3 + i * 0x2545F491) & 0xFFFFFFFF, c["radius"]),
            ]
            if abs(p[0] - ax) > hx or abs(p[2] - az) > hz:
                continue
            if _cre_blocked(view, p):
                continue
            return p
        return [ax, ay, az]
    v = view["reservoirs"][c["habitat"]]
    sx, sz = v["hi"][0] - v["lo"][0], v["hi"][2] - v["lo"][2]
    for i in range(CRE_TRIES):
        fx = h(s1 ^ ((0x57 + i * 0x85EBCA6B) & 0xFFFFFFFF))
        fz = h(s1 ^ ((0x2B + i * 0x2545F491) & 0xFFFFFFFF))
        x, z = v["lo"][0] + (sx * fx >> 32), v["lo"][2] + (sz * fz >> 32)
        p = [x, _cre_terrain_top(view, x, z, v["lo"][1]), z]
        if p[1] > v["hi"][1] or _cre_blocked(view, p):
            continue
        return p
    x, z = v["lo"][0] + sx // 2, v["lo"][2] + sz // 2
    return [x, _cre_terrain_top(view, x, z, v["lo"][1]), z]


def _bfs_path(pairs, a, b):
    """Breadth-first path over (a, b) pairs, lowest-index tie-break, in
    declaration order: the mirror of ws_route's traversal discipline."""
    prev = {a: a}
    queue = [a]
    head = 0
    while head < len(queue):
        at = queue[head]
        head += 1
        if at == b:
            break
        for x, y in pairs:
            nxt = y if x == at else x if y == at else None
            if nxt is None or nxt in prev:
                continue
            prev[nxt] = at
            queue.append(nxt)
    if b not in prev:
        return None
    path = []
    at = b
    while True:
        path.append(at)
        if at == a:
            break
        at = prev[at]
    path.reverse()
    return path


def route_bfs_probe(modules, walk_links, a, b):
    """Walk-only route existence, the compiler mirror of the required-species
    anchor rule (ws_reachable's WALK class for two walk surfaces)."""
    return _bfs_path(walk_links, a, b)


def _cre_route_bfs(view, a, b):
    """Mirror of ws_route: breadth-first path over walk links only, lowest
    index tie-break, returned as the module index path (or None)."""
    return _bfs_path([(l[0], l[1]) for l in view["links"] if l[2] == 0], a, b)


def _cre_derive_slot(view, c, slot, anchored, anchor):
    """Mirror of derive_slot: the placed home plus stations. Anchored
    stations come from the access graph (BFS over walk links, at most two
    hops, lowest (hop, index) first); fauna stations are further biome
    points kept within the species radius of home."""
    home = _cre_home(view, c, slot, anchored, anchor)
    st, mod = [home], [anchor if anchored else None]
    want = c["stations"]
    if anchored:
        depth = {anchor: 0}
        queue = [anchor]
        head = 0
        while head < len(queue) and depth[queue[head]] < CRE_HOPS:
            at = queue[head]
            head += 1
            for l in view["links"]:
                if l[2] != 0:
                    continue
                x, y = l[0], l[1]
                nxt = y if x == at else x if y == at else None
                if nxt is None or nxt in depth:
                    continue
                depth[nxt] = depth[at] + 1
                queue.append(nxt)
        for d in range(1, CRE_HOPS + 1):
            if len(st) >= want:
                break
            for i, m in enumerate(view["modules"]):
                if len(st) >= want:
                    break
                if depth.get(i) != d or "Surface.Walk" not in m.get("tags", []):
                    continue
                st.append(list(m["at"]))
                mod.append(i)
    else:
        v = view["reservoirs"][c["habitat"]]
        sx, sz = v["hi"][0] - v["lo"][0], v["hi"][2] - v["lo"][2]
        s2 = h(c["seed"] ^ (((slot + 1) * 0x68E31DA4) & 0xFFFFFFFF))
        for j in range(1, want):
            fx = h(s2 ^ ((0xE7 + j * 0x85EBCA6B) & 0xFFFFFFFF))
            fz = h(s2 ^ ((0x2D + j * 0x2545F491) & 0xFFFFFFFF))
            x, z = v["lo"][0] + (sx * fx >> 32), v["lo"][2] + (sz * fz >> 32)
            p = [x, _cre_terrain_top(view, x, z, v["lo"][1]), z]
            if (
                p[1] > v["hi"][1]
                or (p[0] - home[0]) ** 2 + (p[2] - home[2]) ** 2 > c["radius"] ** 2
                or _cre_blocked(view, p)
            ):
                p = list(home)
            st.append(p)
            mod.append(None)
    return st, mod, anchored


def creature_at(view, state, species, slot, now_s):
    """Mirror of ws_creature_at: resolve one creature at now_s as a pure
    function of (recipe, exceptions, time). Returns None when the creature
    does not exist. state is None or {"crex": [[id, kind, aux], ...]}."""
    if species >= len(view["creatures"]) or slot >= view["creatures"][species]["slots"]:
        return None
    c = view["creatures"][species]
    cid = creature_id(view, species, slot)
    anchored = c["kind"] != CREATURE_KINDS["fauna"]
    anchor = c["habitat"]
    if state:
        for eid, kind, aux in state["crex"]:
            if eid != cid:
                continue
            if kind == CREX_DEAD:
                return None
            if kind == CREX_RELOCATED:
                if aux >= len(view["modules"]):
                    return None
                anchor, anchored = aux, True
            elif kind == CREX_PINNED:
                st, _, _ = _cre_derive_slot(view, c, slot, anchored, anchor)
                return {"id": cid, "pos": st[0], "species": species, "slot": slot, "station": 0, "traveling": 0}
            break
    st, mod, anchored = _cre_derive_slot(view, c, slot, anchored, anchor)
    period = c["period"]
    s1 = h(c["seed"] ^ (((slot + 1) * 0x9E3779B9) & 0xFFFFFFFF))
    # The C addition wraps at 32 bits exactly like the state clock.
    u = ((now_s + h(s1 ^ 0x5B) % period) & 0xFFFFFFFF) % period
    n = len(st)
    if n <= 1:
        return {"id": cid, "pos": st[0], "species": species, "slot": slot, "station": 0, "traveling": 0}
    w = []
    for j in range(n):
        nxt = 0 if j + 1 == n else j + 1
        if anchored:
            r = route_cost(view["positions"], view["links"], view["features"], mod[j], mod[nxt], CAP_WALK)
            if r is not None:
                w.append(r[0] // CRE_SPEED + 1)
            else:
                w.append(_cre_chord(st[j], st[nxt]) // CRE_SPEED + 1)
        else:
            w.append(_cre_chord(st[j], st[nxt]) // CRE_SPEED + 1)
    total = sum(CRE_DWELL + x for x in w)
    p = u * total // period
    cum = 0
    for j in range(n):
        nxt = 0 if j + 1 == n else j + 1
        if p < cum + CRE_DWELL:
            return {"id": cid, "pos": st[j], "species": species, "slot": slot, "station": j, "traveling": 0}
        cum += CRE_DWELL
        if p < cum + w[j]:
            line = [st[j], st[nxt]]
            if anchored:
                path = _cre_route_bfs(view, mod[j], mod[nxt])
                if path is not None:
                    line = [list(view["modules"][k]["at"]) for k in path]
            length = sum(_cre_chord(line[i], line[i + 1]) for i in range(len(line) - 1))
            d = (p - cum) * length // w[j] if length > 0 else 0
            return {"id": cid, "pos": _cre_point_at(line, d), "species": species, "slot": slot, "station": j, "traveling": 1}
        cum += w[j]
    return {"id": cid, "pos": st[0], "species": species, "slot": slot, "station": 0, "traveling": 0}


def creature_query(view, state, lo, hi, now_s):
    """Mirror of ws_creature_query: creatures inside the window in (species,
    slot) order. Returns the list; the C cap/negative-return contract is
    exercised by the parity gate through ctypes."""
    out = []
    for sp in range(len(view["creatures"])):
        for k in range(view["creatures"][sp]["slots"]):
            one = creature_at(view, state, sp, k, now_s)
            if one is None:
                continue
            p = one["pos"]
            if not (lo[0] <= p[0] <= hi[0] and lo[1] <= p[1] <= hi[1] and lo[2] <= p[2] <= hi[2]):
                continue
            out.append(one)
    return out


def river_lateral(rec, amplitude, k):
    """Mirror of ws_river_sample meander lateral offset."""
    if k <= 0 or k >= 8:
        return 0
    m = 2 * amplitude + 1
    return (h(rec["seed"] ^ (k * 0x9E3779B1 & 0xFFFFFFFF)) % m) - amplitude


def river_plan(rec, t):
    """Mirror of river_plan: position (x, z) at t. Bit-exact with C via
    c_div (C truncation) everywhere C divides signed values."""
    dx, dz = rec["down"][0] - rec["up"][0], rec["down"][2] - rec["up"][2]
    L = math.isqrt(dx * dx + dz * dz)
    a = rec["width"] * 3
    if a > L // 4:
        a = L // 4
    seg = t // 8192
    if seg > 7:
        seg = 7
    u = t - seg * 8192
    # Segment 7 spans 8191 units: blend completes by t=65535, pinning the
    # downstream endpoint exactly (matches river_plan in C).
    span = 8191 if seg == 7 else 8192
    l0, l1 = river_lateral(rec, a, seg), river_lateral(rec, a, seg + 1)
    lat = l0 + c_div((l1 - l0) * u, span)
    return (
        rec["up"][0] + c_div(dx * t, 65535) - c_div(dz * lat, L),
        rec["up"][2] + c_div(dz * t, 65535) + c_div(dx * lat, L),
    )


def river_elevation(rec, t):
    """Mirror of river_elevation: monotonic base slope + typed drops."""
    base, stepped = rec["up"][1] - rec["down"][1], 0
    for e in rec["exceptions"]:
        if e["type"] not in (EXCEPTIONS["waterfall"], EXCEPTIONS["dam"]):
            continue
        base -= e["aux"]
        at = e["at"] + e["length"] if e["type"] == EXCEPTIONS["dam"] else e["at"]
        if t > at:
            stepped += e["aux"]
    return rec["up"][1] - c_div(base * t, 65535) - stepped


def river_sample(rec, t):
    """Bit-exact mirror of ws_river_sample over a compiled feature record."""
    x, z = river_plan(rec, t)
    seg = t // 8192
    if seg > 7:
        seg = 7
    ta = seg * 8192
    tb = 65535 if seg == 7 else (seg + 1) * 8192
    a, b = river_plan(rec, ta), river_plan(rec, tb)
    cx, cz = b[0] - a[0], b[1] - a[1]
    cl = math.isqrt(cx * cx + cz * cz)
    sample = {
        "x": x,
        "y": river_elevation(rec, t),
        "z": z,
        "tangent_x": c_div(cx * 65536, cl) if cl else 65536,
        "tangent_z": c_div(cz * 65536, cl) if cl else 0,
        "width": rec["width"],
        "depth": rec["depth"],
        "flow": rec["flow"],
        "surfaced": 1,
    }
    for e in rec["exceptions"]:
        if t < e["at"] or t > e["at"] + e["length"]:
            continue
        if e["type"] == EXCEPTIONS["rapids"]:
            sample["flow"] = FLOWS["rapid"]
        elif e["type"] == EXCEPTIONS["lake"]:
            sample["width"] = min(rec["width"] * 4, 65535)
            sample["depth"] = min(rec["depth"] * 3, 65535)
            sample["flow"] = FLOWS["calm"]
        elif e["type"] == EXCEPTIONS["underground"]:
            sample["surfaced"] = 0
        elif e["type"] == EXCEPTIONS["dam"]:
            sample["flow"] = FLOWS["calm"]
    return sample


def river_window(rec, lo, hi):
    """Mirror of ws_river_window: the spanning t range able to reach the
    horizontal window, widened for lake reaches (y ignored)."""
    margin = rec["width"] * 4 + 500
    first, last = -1, -1
    for k in range(8):
        a = river_plan(rec, k * 8192)
        b = river_plan(rec, 65535 if k == 7 else (k + 1) * 8192)
        x0, x1 = min(a[0], b[0]), max(a[0], b[0])
        z0, z1 = min(a[1], b[1]), max(a[1], b[1])
        if x1 + margin < lo[0] or x0 - margin > hi[0]:
            continue
        if z1 + margin < lo[2] or z0 - margin > hi[2]:
            continue
        if first < 0:
            first = k * 8192
        last = 65535 if k == 7 else (k + 1) * 8192
    return None if first < 0 else (first, last)


def chord_cross(a, b, c, d):
    """Mirror of the C segment crossing test: where route a-b crosses river
    chord c-d, the route parameter q (16.16), else None. Both segments are
    extended to their endpoints (q and u within 0..65536)."""
    d1x, d1z = b[0] - a[0], b[2] - a[2]
    d2x, d2z = d[0] - c[0], d[2] - c[2]
    den = d1x * d2z - d1z * d2x
    if not den:
        return None
    ax, az = c[0] - a[0], c[2] - a[2]
    q = c_div((ax * d2z - az * d2x) * 65536, den)
    u = c_div((ax * d1z - az * d1x) * 65536, den)
    if not (0 <= q <= 65536 and 0 <= u <= 65536):
        return None
    return q


def river_crossings(rec, a, b):
    """Mirror of river_crossings: how many of the river's eight meander
    segments the straight route fords; touching a meander node counts once
    (sorted q, dedup window 4096)."""
    qs = []
    for k in range(8):
        ta = k * 8192
        tb = 65535 if k == 7 else (k + 1) * 8192
        s0, s1 = river_sample(rec, ta), river_sample(rec, tb)
        q = chord_cross(a, b, (s0["x"], 0, s0["z"]), (s1["x"], 0, s1["z"]))
        if q is not None:
            qs.append(q)
    qs.sort()
    crossings, last = 0, None
    for q in qs:
        if last is None or q - last > 4096:
            crossings += 1
            last = q
    return crossings


def weather(seed, day):
    """Mirror of ws_weather: 0 clear, 1 rain, 2 storm. The state clock wraps
    at 32 bits, so the day hash masks exactly like the C uint32 multiply."""
    return h(seed ^ ((day * 0x9E3779B9 + 0x85EBCA6B) & 0xFFFFFFFF)) % 3


def river_stage(rec, rrecs, state, t):
    """Mirror of ws_river_stage: declared geometry, then the regional water
    field. state is None or a dict with level[i]; returns the sample dict."""
    sample = river_sample(rec, t)
    if not rrecs or state is None:
        return sample
    for i, v in enumerate(rrecs):
        if v["kind"] != RESERVOIR_KINDS["water"]:
            continue
        if not (v["lo"][0] <= sample["x"] <= v["hi"][0] and v["lo"][2] <= sample["z"] <= v["hi"][2]):
            continue
        lvl = min(state["level"][i], v["capacity"])
        if not lvl:
            sample["surfaced"] = 0
            return sample
        rho = lvl * 65536 // v["capacity"]
        w = sample["width"] * rho // 65536
        d = sample["depth"] * rho // 65536
        sample["width"] = w or 1
        sample["depth"] = d or 1
        if rho < 16384:
            for e in rec["exceptions"]:
                if e["type"] == EXCEPTIONS["lake"] and e["at"] <= t <= e["at"] + e["length"]:
                    sample["surfaced"] = 0
                    break
        return sample
    return sample


def resources_tick(state, recs, seed, delta_s):
    """Mirror of ws_resources_tick over compiled reservoir records: rate-based
    recovery scaled by weather and zone (biome/geology), biomass coupled to
    the overlapping Phos stock, capacity-capped inflow, then pit healing in
    creation order. Mutates state in place; sites are dicts."""
    if not delta_s or not recs:
        return
    state["clock_s"] = (state["clock_s"] + delta_s) & 0xFFFFFFFF
    day = state["clock_s"] // 86400
    w = weather(seed, day)
    for i, v in enumerate(recs):
        if state["level"][i] >= v["capacity"]:
            continue
        mult = WEATHER_MULT[v["kind"] - 1][w] * (1 + v["zone"])
        if v["kind"] == RESERVOIR_KINDS["biomass"]:
            for j, o in enumerate(recs):
                if o["kind"] != RESERVOIR_KINDS["phos"]:
                    continue
                if v["lo"][0] >= o["hi"][0] or o["lo"][0] >= v["hi"][0] or v["lo"][2] >= o["hi"][2] or o["lo"][2] >= v["hi"][2]:
                    continue
                mult *= 1 + state["level"][j] // o["capacity"]
                break
        inflow = v["rate"] * mult * delta_s // 86400
        add = min(inflow, v["capacity"] - state["level"][i])
        state["level"][i] += add
        state["recovered"] += add
    for i in range(len(recs)):
        j = 0
        while j < len(state["sites"]):
            site = state["sites"][j]
            if site["reservoir"] != i or site["kind"] != 0 or not site["amount"]:
                j += 1
                continue
            heal = min(site["amount"], state["level"][i])
            if not heal:
                j += 1
                continue
            site["amount"] -= heal
            state["level"][i] -= heal
            state["lost"] += heal
            if not site["amount"]:
                del state["sites"][j]
            else:
                j += 1


def ledger_check(state, recs):
    """Mirror of ws_ledger_check: the conservation identity over regional
    stocks, carried material and shards, used and lost units."""
    n = len(recs)
    lhs = sum(state["level"][:n]) + sum(state["material"][: state["player_count"]]) + sum(state["shards"][: state["player_count"]])
    lhs += state["used"] + state["lost"]
    rhs = sum(v["level"] for v in recs) + state["recovered"]
    return lhs == rhs


def runtime_view(src):
    """Resolve a source recipe into the runtime numbers C sees: materialized
    module positions, index-resolved links (a, b, kind, anchor) with the
    anomaly anchor as a reservoir index, compiled feature and reservoir
    records."""
    ids = {m["name"]: i for i, m in enumerate(src["modules"])}
    modules = [materialized(src, m) for m in src["modules"]]
    rindex = {v["name"]: i for i, v in enumerate(src.get("reservoirs", []))}
    links = []
    for link in src.get("links", []):
        if len(link) == 4:
            a, b, kind, anchor = link
            if kind != "anomaly":
                raise ValueError("only anomaly links carry an anchor")
            if anchor not in rindex:
                raise ValueError("anomaly anchor must be a declared reservoir")
            links.append((ids[a], ids[b], LINK_KINDS[kind], rindex[anchor]))
        else:
            a, b, kind = link
            links.append((ids[a], ids[b], LINK_KINDS[kind], None))
    features = [feature_record(f) for f in src.get("features", [])]
    reservoirs = []
    for i, v in enumerate(src.get("reservoirs", [])):
        rec = reservoir_record(v)
        rec["id"] = child([bounded(a, 0, 0xFFFFFFFF, "ancestry") for a in src["ancestry"]], bounded(v.get("key", 200 + i), 1, 0xFFFFFFFF, "reservoir key"))
        reservoirs.append(rec)
    return modules, links, features, reservoirs


def link_cost(modules, links, features, i, caps=CAP_WALK):
    """Mirror of ws_link_cost (None = capability gated). Links are the
    runtime 4-tuples (a, b, kind, anchor)."""
    a, b, kind, _ = links[i]
    if kind == 1 and not caps & CAP_LIFT:
        return None
    if kind == 2 and not caps & CAP_PORTAL:
        return None
    if kind == 3:
        if not caps & CAP_ANOMALY:
            return None
        return ANOMALY_COST
    A, B = modules[a], modules[b]
    dx, dy, dz = B[0] - A[0], B[1] - A[1], B[2] - A[2]
    climb = abs(dy)
    if kind == 1:
        return 500 + climb // 2
    if kind == 2:
        return 2000
    cost = math.isqrt(dx * dx + dz * dz) + 8 * climb
    for rec in features:
        cost += 20000 * river_crossings(rec, A, B)
    return cost


def route_cost(modules, links, features, a, b, caps=CAP_WALK):
    """Mirror of ws_route_cost: deterministic Dijkstra with lowest-index
    tie-break over declared topology; returns (cost, path) or None when
    unreachable."""
    n = len(modules)
    dist = [None] * n
    prev = [None] * n
    done = [False] * n
    dist[a] = 0
    while True:
        u = -1
        for i in range(n):
            if not done[i] and dist[i] is not None and (u < 0 or dist[i] < dist[u]):
                u = i
        if u < 0:
            break
        done[u] = True
        for i, link in enumerate(links):
            if link[0] == u and not done[link[1]]:
                v = link[1]
            elif link[1] == u and not done[link[0]]:
                v = link[0]
            else:
                continue
            c = link_cost(modules, links, features, i, caps)
            if c is None:
                continue
            if dist[v] is None or dist[u] + c < dist[v]:
                dist[v] = dist[u] + c
                prev[v] = u
    if dist[b] is None:
        return None
    path = []
    at = b
    while True:
        path.append(at)
        if at == a:
            break
        at = prev[at]
    path.reverse()
    return dist[b], path


def link_open(links, reservoirs, state, i):
    """Mirror of ws_link_open: 1 iff link i is an open anomaly gate. The
    gate holds while the anchored Phos stock keeps at least half the region
    capacity; a bound state dict decides, otherwise the declared levels."""
    if i >= len(links) or links[i][2] != 3:
        return 0
    anchor = links[i][3]
    if anchor >= len(reservoirs):
        return 0
    v = reservoirs[anchor]
    if v["kind"] != RESERVOIR_KINDS["phos"]:
        return 0
    level = v["level"]
    if state is not None and len(state["level"]) == len(reservoirs):
        level = min(state["level"][anchor], v["capacity"])
    return int(2 * level >= v["capacity"])


def route_cost_state(modules, links, features, reservoirs, state, a, b, caps=CAP_WALK):
    """Mirror of ws_route_cost_state: the same Dijkstra as route_cost with
    closed gates absent (openness from the bound state, or declared levels
    with state None)."""
    n = len(modules)
    dist = [None] * n
    prev = [None] * n
    done = [False] * n
    dist[a] = 0
    while True:
        u = -1
        for i in range(n):
            if not done[i] and dist[i] is not None and (u < 0 or dist[i] < dist[u]):
                u = i
        if u < 0:
            break
        done[u] = True
        for i, link in enumerate(links):
            if link[0] == u and not done[link[1]]:
                v = link[1]
            elif link[1] == u and not done[link[0]]:
                v = link[0]
            else:
                continue
            if link[2] == 3 and not link_open(links, reservoirs, state, i):
                continue
            c = link_cost(modules, links, features, i, caps)
            if c is None:
                continue
            if dist[v] is None or dist[u] + c < dist[v]:
                dist[v] = dist[u] + c
                prev[v] = u
    if dist[b] is None:
        return None
    path = []
    at = b
    while True:
        path.append(at)
        if at == a:
            break
        at = prev[at]
    path.reverse()
    return dist[b], path


# ---- Gate 6: directed social evidence and projections (pure mirrors) ----
# Records are retained causal capsules (dicts with observer, subject,
# teller, root, kind, confidence, context, clock, salience, valence); views
# are pure functions of (records, clock). Nothing ranks people: each answer
# is one directed observer->subject estimate with its counts, and an
# observer without records is unchanged.

EV_CAP = 64
EV_PLAYER = 0x10000
EV_PINNED = 500
EV_DECAY_S = 86400
EV_REPORTED_MAX = 750
EV_REPORTED_SHARE = 3
PEND_CAP = 8
REPORT_DELAY_S = 3600
FEED_CAP = 16
EV_ACT, EV_CLAIM, EV_CONTRADICT, EV_RESTITUTION, EV_PROMISE = 1, 2, 3, 4, 5
EV_KEPT, EV_BREACH, EV_REPORT = 6, 7, 8
CTX_NONE, CTX_ARENA, CTX_DEFENSE, CTX_RESTITUTION = 0, 1, 2, 3
CTX_SHOWN, CTX_PROMISE, CTX_DISPATCH = 4, 5, 6
QUIET = 0xFFFF
FEED_FACTION_NOTICE = 5
# Authority-assigned contextual valence per committed action (mirror of
# WS_VALENCE): what a witness's projection makes of the act itself.
ACTION_VALENCE = {
    1: 0, 2: 300, 3: -300, 4: 100, 5: 0, 6: 0, 7: 300, 8: -500,
    9: 50, 10: 200, 11: -100, 12: 0, 13: 0, 14: 50, 15: 0, 16: 0, 17: 100,
}


def _trunc_div(a, b):
    """C integer division truncates toward zero."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def effective(rec):
    """Interpretation before aggregation: consensual or defensive force
    counts at half weight, restitution doubles its own positive
    contribution, claims carry no valence at all."""
    v = rec["valence"]
    if rec["context"] in (CTX_ARENA, CTX_DEFENSE) and v < 0:
        v = _trunc_div(v, 2)
    if rec["context"] == CTX_RESTITUTION and v > 0:
        v *= 2
    return max(-1000, min(1000, v))


def social_view(records, clock):
    """Mirror of ws_social_view: fixed integer decay buckets against stored
    anchors (pinned records skip decay, minor history beyond the window is
    gone), so query frequency, save/reload and C/Python all agree."""
    out = {
        "trust": 0, "acts": 0, "claims": 0, "contradictions": 0,
        "restitutions": 0, "promises": 0, "kept": 0, "breaches": 0,
        "reports": 0, "confidence": 0, "watermark": 0,
    }
    counts = {
        EV_ACT: "acts", EV_CLAIM: "claims", EV_CONTRADICT: "contradictions",
        EV_RESTITUTION: "restitutions", EV_PROMISE: "promises",
        EV_KEPT: "kept", EV_BREACH: "breaches", EV_REPORT: "reports",
    }
    for rec in records[:EV_CAP]:
        if rec["observer"] < 0 or rec["subject"] < 0:
            continue
        bucket = 0 if rec["salience"] >= EV_PINNED else (clock - rec["clock"]) // EV_DECAY_S
        if bucket > 15:
            continue
        weight = rec["confidence"] >> bucket
        out["trust"] += _trunc_div(effective(rec) * weight, 1000)
        out["confidence"] += weight
        out["watermark"] = max(out["watermark"], rec["root"])
        out[counts[rec["kind"]]] += 1
    out["trust"] = max(-1000, min(1000, out["trust"]))
    out["confidence"] = min(1000, out["confidence"])
    return out


def social_cite(records):
    """Mirror of ws_social_cite: strongest retained cause, ties to the
    latest committed root; None is the honest empty limit."""
    found = None
    for rec in records[:EV_CAP]:
        found = rec if found is None or rec["salience"] > found["salience"] or (
            rec["salience"] == found["salience"] and rec["root"] > found["root"]
        ) else found
    return None if found is None else (found["root"], found["kind"], found["salience"])


def evidence_put(records, rec):
    """Mirror of ws_evidence_put: a repeated (observer, root, teller, kind)
    replaces instead of appending; the table is a bounded ring."""
    for i, at in enumerate(records):
        if (at["observer"], at["root"], at["teller"], at["kind"]) == (
            rec["observer"], rec["root"], rec["teller"], rec["kind"]
        ):
            records[i] = dict(rec)
            return
    if len(records) == EV_CAP:
        del records[0]
    records.append(dict(rec))


def social_settle(m):
    """Mirror of ws_social_settle over a mirror state dict (ev, pend, feed,
    revision, clock_s, players): due dispatches deliver degraded
    REPORT-class evidence to the clerk, the public feed records the
    faction's receipt, and each delivery is its own canonical revision."""
    i = 0
    while i < len(m["pend"]):
        p = m["pend"][i]
        if m["clock_s"] < p["deliver_s"]:
            i += 1
            continue
        conf = p["confidence"] * EV_REPORTED_SHARE // 4
        delivered = {
            "observer": p["observer"], "subject": p["subject"],
            "teller": p["teller"], "root": p["root"], "kind": EV_REPORT,
            "confidence": min(EV_REPORTED_MAX, conf), "context": p["context"],
            "clock": m["clock_s"], "salience": p["salience"],
            "valence": p["valence"],
        }
        del m["pend"][i]
        m["revision"] += 1
        teller = p["teller"]
        actor = m["players"][teller - EV_PLAYER]["id"] if EV_PLAYER <= teller < EV_PLAYER + len(m["players"]) else 0
        evidence_put(m["ev"], delivered)
        if len(m["feed"]) == FEED_CAP:
            del m["feed"][0]
        m["feed"].append({
            "revision": m["revision"], "actor": actor, "epoch": m["revision"],
            "target": p["observer"] - 1, "kind": FEED_FACTION_NOTICE,
        })


def ev_handle_ok(m, h):
    """Mirror of ws_ev_handle_ok over a mirror state dict (npcs: set of
    entity indexes flagged NPC, players: list of joined players)."""
    if not h:
        return False
    if h >= EV_PLAYER:
        return h - EV_PLAYER < len(m["players"])
    return h <= m["npc_count"] and (h - 1) in m["npcs"]


def witness_confidence(observer, event):
    """Mirror of the shared witness gate for open sightlines: confidence is
    (30000 - manhattan) // 30, and 0 beyond the 30000 mm window. Occlusion
    is engine geometry (proved by the C fixture); scenarios that need it
    declare the occluded pairs themselves."""
    d = sum(abs(a - b) for a, b in zip(observer, event))
    if d >= 30000:
        return 0
    return (30000 - d) * 1000 * 1000 // 30_000_000


def compile_recipe(src):
    if set(src) - {
        "schema",
        "generator",
        "ancestry",
        "seed",
        "epoch",
        "revision",
        "name",
        "modules",
        "links",
        "features",
        "reservoirs",
        "creatures",
    }:
        raise ValueError("unknown source field: SCHEMA_EXTENSION required")
    if src["schema"] != SCHEMA or src["generator"] != GENERATOR:
        raise ValueError("unsupported schema/generator")
    ancestry = [bounded(v, 0, 0xFFFFFFFF, "ancestry") for v in src["ancestry"]]
    if len(ancestry) != 4:
        raise ValueError("128-bit ancestry required")
    modules = src["modules"]
    links = src.get("links", [])
    if not 1 <= len(modules) <= 128 or len(links) > 256:
        raise ValueError("product capacity exceeded")
    ids = {}
    keys = set()
    handles = set()
    body = bytearray()
    manifest = []
    for i, m in enumerate(modules):
        if set(m) - {
            "name",
            "key",
            "parent",
            "level",
            "shape",
            "tags",
            "phos",
            "position",
            "size",
            "color",
            "variation_seed",
            "jitter",
            "affinity",
            "quantity",
        }:
            raise ValueError("unknown module field")
        name = m["name"]
        key = bounded(m["key"], 1, 0xFFFFFFFF, "key")
        parent = m.get("parent")
        pi = ids[parent] if parent else 65535
        if name in ids or (pi, key) in keys:
            raise ValueError("duplicate name or sibling key")
        ident = child(manifest[pi]["id"] if parent else ancestry, key)
        if tuple(ident) in handles:
            raise ValueError("identity collision")
        ids[name] = i
        keys.add((pi, key))
        handles.add(tuple(ident))
        flags = 0
        for t in m.get("tags", []):
            flags |= TAGS[t]
        kind = SHAPES[m["shape"]] | LEVELS.index(m.get("level", "object")) << 8
        pos = [bounded(v, -1000000, 1000000, "position") for v in m["position"]]
        size = [bounded(v, 1, 400000, "size") for v in m["size"]]
        jitter = [bounded(v, -32768, 32767, "jitter") for v in m.get("jitter", [0, 0])]
        if len(pos) != 3 or len(size) != 3 or len(jitter) != 2:
            raise ValueError("coordinate dimensions")
        body += struct.pack(
            "<4I4H6i2I2h2H",
            *ident,
            pi,
            kind,
            flags,
            PHOS[m.get("phos", "Neutral")],
            *pos,
            *size,
            bounded(m.get("color", 0x888888), 0, 0xFFFFFF, "color"),
            bounded(m.get("variation_seed", key), 0, 0xFFFFFFFF, "seed"),
            *jitter,
            bounded(m.get("affinity", 0), 0, 1000, "affinity"),
            bounded(m.get("quantity", 0), 0, 65535, "quantity"),
        )
        manifest.append({"name": name, "id": ident, "index": i})
    graph = {
        i: set() for i, m in enumerate(modules) if "Surface.Walk" in m.get("tags", [])
    }
    parents = []
    walk_links = []
    anomaly_links = []
    rindex = {v["name"]: i for i, v in enumerate(src.get("reservoirs", []))}
    for link in links:
        if len(link) == 4:
            a, b, kind, anchor = link
            if kind != "anomaly":
                raise ValueError("only anomaly links carry an anchor")
            if anchor not in rindex:
                raise ValueError("anomaly anchor must be a declared reservoir")
            reserved = rindex[anchor]
        else:
            a, b, kind = link
            anchor, reserved = None, 0
        if kind not in LINK_KINDS:
            raise ValueError(f"unknown link kind: {kind}")
        a, b = ids[a], ids[b]
        k = LINK_KINDS[kind]
        if a == b or a not in graph or b not in graph:
            raise ValueError("illegal navigation endpoint")
        graph[a].add(b)
        graph[b].add(a)
        if k == 0:
            validate_walk_edge(src, modules, a, b)
            walk_links.append((a, b))
        if k == 3:
            anomaly_links.append((a, b, reserved))
        body += struct.pack("<4H", a, b, k, reserved)
    # Sparse world-scale features (rivers and typed exceptions): stable
    # identity from the ancestry like modules, validated fail-closed, packed
    # after the links. A product carries features only when declared; the
    # legacy schema 1 layout stays byte-identical for all older artifacts.
    fnames = set()
    frecords = []
    for i, f in enumerate(src.get("features", [])):
        name = f.get("name")
        if not name or name in ids or name in fnames:
            raise ValueError("duplicate or missing feature name")
        fnames.add(name)
        key = bounded(f.get("key", 100 + i), 1, 0xFFFFFFFF, "feature key")
        ident = child(ancestry, key)
        if tuple(ident) in handles:
            raise ValueError("identity collision")
        handles.add(tuple(ident))
        rec = feature_record(f)
        rec["id"] = ident
        rec["index"] = len(frecords)
        frecords.append(rec)
    if len(frecords) > 8:
        raise ValueError("feature capacity exceeded")
    ecount = sum(len(rec["exceptions"]) for rec in frecords)
    if ecount > 24:
        raise ValueError("exception capacity exceeded")
    fbody = bytearray()
    for rec in frecords:
        fbody += struct.pack(
            "<4I2H6i2H2I",
            *rec["id"],
            rec["kind"],
            rec["flow"],
            *rec["up"],
            *rec["down"],
            rec["width"],
            rec["depth"],
            rec["seed"],
            0,
        )
    ebody = bytearray()
    for rec in frecords:
        for e in rec["exceptions"]:
            ebody += struct.pack("<4HI", rec["index"], e["type"], e["at"], e["length"], e["aux"])
    # Regional reservoirs (schema 3): stable identity from the ancestry,
    # validated fail-closed, packed after the exception table. Same-kind
    # overlap is rejected here exactly as ws_validate rejects it, because
    # overlapping water regions would make stage lookup ambiguous.
    rnames = set()
    rrecords = []
    for i, v in enumerate(src.get("reservoirs", [])):
        name = v.get("name")
        if not name or name in ids or name in fnames or name in rnames:
            raise ValueError("duplicate or missing reservoir name")
        rnames.add(name)
        key = bounded(v.get("key", 200 + i), 1, 0xFFFFFFFF, "reservoir key")
        ident = child(ancestry, key)
        if tuple(ident) in handles:
            raise ValueError("identity collision")
        handles.add(tuple(ident))
        rec = reservoir_record(v)
        rec["id"] = ident
        rec["index"] = len(rrecords)
        rrecords.append(rec)
    if len(rrecords) > 8:
        raise ValueError("reservoir capacity exceeded")
    for i, a in enumerate(rrecords):
        for b in rrecords[:i]:
            if a["kind"] != b["kind"]:
                continue
            if (
                a["lo"][0] < b["hi"][0]
                and b["lo"][0] < a["hi"][0]
                and a["lo"][1] < b["hi"][1]
                and b["lo"][1] < a["hi"][1]
                and a["lo"][2] < b["hi"][2]
                and b["lo"][2] < a["hi"][2]
            ):
                raise ValueError("same-kind reservoir regions overlap")
    # Nonlocal anomaly gates, mirroring ws_validate fail-closed: the anchor
    # must be a Phos region, the gate seat stands inside it, the far end
    # outside it, the two ends are conventionally nonadjacent (no ordinary
    # path of fewer than three links), and one Phos region hosts one gate.
    def ordinary_hops(a, b):
        seen = {a: 0}
        todo = [a]
        while todo:
            at = todo.pop(0)
            for x, y, kind, _ in resolved_links:
                if kind == 3:
                    continue
                nxt = y if x == at else x if y == at else None
                if nxt is None or nxt in seen:
                    continue
                seen[nxt] = seen[at] + 1
                todo.append(nxt)
        return seen.get(b, 256)

    resolved_links = runtime_view(src)[1]
    for a, b, reserved in anomaly_links:
        v = rrecords[reserved]
        if v["kind"] != RESERVOIR_KINDS["phos"]:
            raise ValueError("anomaly anchor must be a Phos reservoir")
        if sum(1 for _, _, r in anomaly_links if r == reserved) > 1:
            raise ValueError("one anomaly gate per Phos region")
        A, B = materialized(src, modules[a]), materialized(src, modules[b])
        if not (v["lo"][0] <= A[0] <= v["hi"][0] and v["lo"][2] <= A[2] <= v["hi"][2]):
            raise ValueError("anomaly gate seat must stand inside the anchored region")
        if v["lo"][0] <= B[0] <= v["hi"][0] and v["lo"][2] <= B[2] <= v["hi"][2]:
            raise ValueError("anomaly far end must stand outside the anchored region")
        if ordinary_hops(a, b) < 3:
            raise ValueError("anomaly gate must link conventionally nonadjacent ends")
    rbody = bytearray()
    for rec in rrecords:
        rbody += struct.pack(
            "<4I2H6i5I",
            *rec["id"],
            rec["kind"],
            rec["zone"],
            *rec["lo"],
            *rec["hi"],
            rec["capacity"],
            rec["level"],
            rec["rate"],
            0,
            0,
        )
    # Sparse creature species (schema 4): stable identity from the ancestry,
    # validated fail-closed, packed after the reservoir table. Anchored
    # species need a walk-surface anchor that is not an interior; required
    # species must be baseline-walk-reachable from the first declared walk
    # surface, mirroring ws_validate exactly.
    cnames = set()
    crecs = []
    for i, c in enumerate(src.get("creatures", [])):
        name = c.get("name")
        if not name or name in ids or name in fnames or name in rnames or name in cnames:
            raise ValueError("duplicate or missing creature name")
        cnames.add(name)
        key = bounded(c.get("key", 300 + i), 1, 0xFFFFFFFF, "creature key")
        ident = child(ancestry, key)
        if tuple(ident) in handles:
            raise ValueError("identity collision")
        handles.add(tuple(ident))
        rec = creature_record(c, ids, rindex)
        if len(crecs) >= 16:
            raise ValueError("creature capacity exceeded")
        rec["id"] = ident
        rec["index"] = len(crecs)
        crecs.append(rec)
    if crecs:
        first_walk = next(
            (i for i, m in enumerate(modules) if "Surface.Walk" in m.get("tags", [])),
            None,
        )
        walk_links_only = [(a, b) for a, b, kind, _ in resolved_links if kind == 0]
        for rec in crecs:
            if rec["kind"] == CREATURE_KINDS["fauna"]:
                continue
            m = modules[rec["habitat"]]
            if "Surface.Walk" not in m.get("tags", []):
                raise ValueError("creature anchor must be a walk surface")
            if "Scope.Interior" in m.get("tags", []):
                raise ValueError("creature anchor must be exterior")
            if rec["kind"] == CREATURE_KINDS["required"]:
                if first_walk is None:
                    raise ValueError("required creature anchor needs a walk network")
                probe = route_bfs_probe(modules, walk_links_only, first_walk, rec["habitat"])
                if probe is None:
                    raise ValueError("required creature anchor must be baseline-walk-reachable")
    cbody = bytearray()
    for rec in crecs:
        cbody += struct.pack(
            "<4I6HI",
            *rec["id"],
            rec["kind"],
            rec["habitat"],
            rec["slots"],
            rec["period"],
            rec["stations"],
            rec["radius"],
            rec["seed"],
        )
    if graph:
        seen = set()
        todo = [next(iter(graph))]
        while todo:
            a = todo.pop()
            if a not in seen:
                seen.add(a)
                todo.extend(graph[a] - seen)
        if seen != set(graph):
            raise ValueError("disconnected walk surfaces")
    # Baseline public access, mirroring ws_topology: an NPC (vendor) may not
    # be sealed inside a room with no access ports. A room has a port when
    # the default public south entrance fits, or a declared walk edge crosses
    # one of its walls (validate_walk_edge already proved that port fits).
    for i, m in enumerate(modules):
        pi = ids[m["parent"]] if m.get("parent") else 65535
        parents.append(pi)
    for i, m in enumerate(modules):
        if "Entity.NPC" not in m.get("tags", []):
            continue
        room = i if m["shape"] == "room" else parents[i]
        if room == 65535 or room >= len(modules) or modules[room]["shape"] != "room":
            continue
        h = modules[room]
        if h["size"][0] // 2 >= 2000 + 300:
            continue
        A = materialized(src, h)
        hx, hz = h["size"][0] // 2, h["size"][2] // 2
        doorway = False
        for a, b in walk_links:
            if a != room and b != room:
                continue
            o = materialized(src, modules[b if a == room else a])
            if abs(o[0] - A[0]) > hx or abs(o[2] - A[2]) > hz:
                doorway = True
                break
        if not doorway:
            raise ValueError("Entity.NPC sealed inside a room with no public access port")
    schema = (
        SCHEMA_CREATURES
        if crecs
        else SCHEMA_RESOURCES
        if rrecords
        else SCHEMA_FEATURES
        if frecords
        else SCHEMA
    )
    if crecs:
        counts = (len(modules), len(links), len(frecords), ecount, len(rrecords), len(crecs))
    elif rrecords:
        counts = (len(modules), len(links), len(frecords), ecount, len(rrecords))
    elif frecords:
        counts = (len(modules), len(links), len(frecords), ecount)
    else:
        counts = (len(modules), len(links))
    suffix = (
        struct.pack(
            "<4I3I" + "H" * len(counts),
            *ancestry,
            *[
                bounded(src.get(k, 0), 0, 0xFFFFFFFF, k)
                for k in ("seed", "epoch", "revision")
            ],
            *counts,
        )
        + body
        + bytes(fbody)
        + bytes(ebody)
        + bytes(rbody)
        + bytes(cbody)
    )
    data = (
        struct.pack(
            "<4sHHII",
            b"CWS1",
            schema,
            GENERATOR,
            len(suffix) + 16,
            zlib.crc32(suffix),
        )
        + suffix
    )
    return data, {
        "schema": schema,
        "generator": GENERATOR,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "modules": manifest,
        "links": len(links),
        "features": [f.get("name") for f in src.get("features", [])],
        "exceptions": ecount,
        "reservoirs": [v.get("name") for v in src.get("reservoirs", [])],
        "creatures": [c.get("name") for c in src.get("creatures", [])],
        "source_sha256": hashlib.sha256(
            json.dumps(src, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest(),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("command", choices=["schema", "validate", "compile", "inspect"])
    ap.add_argument("source", nargs="?")
    ap.add_argument("--output")
    ap.add_argument("--c-include")
    ap.add_argument("--c-symbol", default="ws_product")
    args = ap.parse_args()
    try:
        if args.command == "schema":
            result = {
                "schema": SCHEMA,
                "generator": GENERATOR,
                "tags": TAGS,
                "phos": PHOS,
                "shapes": SHAPES,
                "levels": LEVELS,
                "limits": {"modules": 128, "links": 256},
                "permission": "CONTENT; unknown fields require SCHEMA_EXTENSION",
            }
        else:
            src = json.loads(pathlib.Path(args.source).read_text())
            data, result = compile_recipe(src)
            if args.command == "compile":
                if not args.output:
                    raise ValueError("--output required")
                out = pathlib.Path(args.output)
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(data)
                out.with_suffix(".manifest.json").write_text(
                    json.dumps(result, indent=2) + "\n"
                )
                if args.c_include:
                    pathlib.Path(args.c_include).write_text(
                        "/* Generated by tools/worldsdk/sdk.py. Do not edit. */\n"
                        f"static const unsigned char {args.c_symbol}[]={{\n"
                        + "".join(
                            ",".join(map(str, data[i : i + 24])) + ",\n"
                            for i in range(0, len(data), 24)
                        )
                        + "};\n"
                    )
        print(json.dumps({"ok": True, **result}, sort_keys=True))
    except (ValueError, KeyError, TypeError, IndexError, OSError) as e:
        print(
            json.dumps(
                {
                    "ok": False,
                    "error": str(e),
                    "permission": "CONTENT or declared SCHEMA_EXTENSION",
                }
            )
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
