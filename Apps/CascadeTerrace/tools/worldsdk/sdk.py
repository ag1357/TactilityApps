#!/usr/bin/env python3
"""Bounded World SDK source compiler. Standard library only. No runtime JSON."""

import argparse, hashlib, json, pathlib, struct, sys, zlib

SCHEMA = GENERATOR = 1
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
    for a, b, kind in links:
        a, b = ids[a], ids[b]
        k = {"walk": 0, "lift": 1, "portal": 2}[kind]
        if a == b or a not in graph or b not in graph:
            raise ValueError("illegal navigation endpoint")
        graph[a].add(b)
        graph[b].add(a)
        if k == 0:
            validate_walk_edge(src, modules, a, b)
            walk_links.append((a, b))
        body += struct.pack("<4H", a, b, k, 0)
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
    suffix = (
        struct.pack(
            "<4I3I2H",
            *ancestry,
            *[
                bounded(src.get(k, 0), 0, 0xFFFFFFFF, k)
                for k in ("seed", "epoch", "revision")
            ],
            len(modules),
            len(links),
        )
        + body
    )
    data = (
        struct.pack(
            "<4sHHII", b"CWS1", SCHEMA, GENERATOR, len(suffix) + 16, zlib.crc32(suffix)
        )
        + suffix
    )
    return data, {
        "schema": SCHEMA,
        "generator": GENERATOR,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "modules": manifest,
        "links": len(links),
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
                        "/* Generated by tools/worldsdk/sdk.py. Do not edit. */\nstatic const unsigned char ws_product[]={\n"
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
