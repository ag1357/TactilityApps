#!/usr/bin/env python3
"""Macro geography generator (mission §49). Everything below is DERIVED from
the generated geography: the river record is placed first, then the valley
trail, the ford, the wetland, the karst ridge, the dam, the ruin and both
settlements are computed FROM the river samples (perpendicular bank offsets,
elevation following the water, the ford where the river comes closest to the
ruin). No hand-positioned route. One route-cost case is required by the
mission: the geometrically nearest settlement must not be the cheapest
reachable destination, because of climb and fording costs over declared
topology — terrain, not straight lines, decides.

Output: a schema-2 World SDK source (sparse river record + exceptions +
modules + walk links), compiled product bytes, and a derivation summary.
Standard library only."""

import argparse, json, math, pathlib, sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from sdk import CAP_WALK, compile_recipe, feature_record, h, river_sample, route_cost, runtime_view

ROOT = pathlib.Path(__file__).resolve().parents[2]

SHOWCASE = 0x9C0D51  # committed macro world seed

# Trail t stations (curve parameter): col area, upper valley, above the lake,
# karst country, lower valley; the ford is derived separately below.
EAST_T = (8000, 20000, 30000, 44000, 48000)
FORD_SCAN = (53000, 57000)  # post-lake, pre-dam reach
BANK = 120000  # trail offset from the river centreline (mm)
RISE = 8000  # trail elevation above the water surface (mm)


def wob(seed, span, mask):
    """Symmetric derived offset in [-span, span]."""
    return h(seed ^ mask) % (2 * span + 1) - span


def perp(tx, tz):
    """Unit perpendicular (16.16) of a tangent, pointing east of the flow."""
    return -tz, tx


def bank_point(rec, t, offset, rise):
    """A point offset from the river centreline along the local perpendicular,
    lifted above the water: bank geography derived from the river itself."""
    s = river_sample(rec, t)
    px, pz = perp(s["tangent_x"], s["tangent_z"])
    return [
        s["x"] + px * offset // 65536,
        s["y"] + rise,
        s["z"] + pz * offset // 65536,
    ]


def solid(name, position, size, level="structure", color=0x6B5D4F):
    return {
        "name": name,
        "level": level,
        "shape": "box",
        "tags": ["Collision.Solid"],
        "position": list(position),
        "size": list(size),
        "color": color,
    }


def plane(name, position, size=(140000, 400, 140000), **kw):
    return {
        "name": name,
        "shape": "plane",
        "tags": ["Surface.Walk"],
        "position": list(position),
        "size": list(size),
        **kw,
    }


def vista(name, position, size, level="region", color=0x7A8B5A):
    """Visual region module: no walk surface, no collision, no gameplay."""
    return {
        "name": name,
        "level": level,
        "shape": "plane",
        "tags": [],
        "position": list(position),
        "size": list(size),
        "color": color,
    }


def generate(seed=SHOWCASE, variant=0):
    """Deterministic macro geography. `variant` is the derived-retry counter:
    compile-time realizability (unblocked direct routes, port fits, slope
    bounds, the route-cost inequality) is re-derived, never hand-fixed."""
    s = seed ^ (variant * 0x9E37)
    # --- mountain massif and the col between its shoulders ---
    mx = wob(s, 80000, 0xA1)
    mountain = solid(
        "mountain",
        (mx, 600000, -760000),
        (400000, 400000, 360000),
        level="region",
        color=0x8A8F96,
    )
    shoulder_e = solid("shoulder_east", (mx + 350000, 520000, -700000), (220000, 400000, 300000), color=0x7D838C)
    shoulder_w = solid("shoulder_west", (mx - 350000, 520000, -700000), (220000, 400000, 300000), color=0x7D838C)
    # --- the river: watershed at the col, mouth at the south edge ---
    up = [mx + wob(s, 60000, 0xB2), 520000, -560000]
    down = [mx + wob(s, 90000, 0xB3), 40000, 930000]
    river_src = {
        "name": "river_cascade",
        "key": 21,
        "kind": "river",
        "flow": "calm",
        "upstream": up,
        "downstream": down,
        "width": 12000,
        "depth": 900,
        "seed": (s ^ 0x77) & 0xFFFFFFFF,
        "exceptions": [
            {"type": "waterfall", "at": 0.05, "drop": 170000},
            {"type": "rapids", "at": 0.22, "length": 0.05},
            {"type": "lake", "at": 0.50, "length": 0.11},
            {"type": "underground", "at": 0.66, "length": 0.07},
            {"type": "dam", "at": 0.90, "length": 0.02, "drop": 14000},
        ],
    }
    rec = feature_record(river_src)
    # --- valley trail on the east bank, derived from the river itself ---
    east = [bank_point(rec, t, BANK, RISE) for t in EAST_T]
    # --- the ruin: upper-valley wilderness east of the trail, derived from
    # the river sample at its spur station extended along the perpendicular.
    # It sits near the gate (north) and far from the haven (south), so the
    # mission's cost case is decided by terrain, not straight lines. ---
    ruin = plane(
        "ruin",
        bank_point(rec, 26000, BANK + 70000 + wob(s, 20000, 0xC4), RISE + 4000),
        (60000, 400, 60000),
        level="structure",
        tags=["Surface.Walk", "Action.Repair"],
        color=0x9C8F7A,
    )
    # --- the ford: where the river comes closest to the ruin, below the
    # lake, above the dam ---
    best_t, best_d = None, None
    for t in range(FORD_SCAN[0], FORD_SCAN[1] + 1, 128):
        p = river_sample(rec, t)
        d = (p["x"] - ruin["position"][0]) ** 2 + (p["z"] - ruin["position"][2]) ** 2
        if best_d is None or d < best_d:
            best_t, best_d = t, d
    ford_t = best_t
    e_ford = bank_point(rec, ford_t, BANK, RISE)
    w_ford = bank_point(rec, ford_t, -BANK, RISE)
    # --- west bank down to the mouth settlement ---
    w1 = bank_point(rec, 62000, -BANK, RISE)
    haven = bank_point(rec, 64500, -(BANK + 60000), RISE + 2000)
    # --- high gate settlement at the col's east shoulder foot ---
    gate = [
        shoulder_e["position"][0] + wob(s, 20000, 0xD5),
        shoulder_e["position"][1] - 50000,
        shoulder_e["position"][2] + 220000,
    ]
    # --- derived anchors: watershed viewpoint, karst ridge over the
    # underground reach, dam wall at the dam reach, lake wetland ---
    watershed = plane("watershed", [up[0], up[1] + 2000, up[2] + 12000], (60000, 400, 60000), level="structure", color=0xB9C4CF)
    # The anchor t values mirror feature_record's fraction conversion, so the
    # boxes sit exactly over the reaches they annotate.
    under_a = river_sample(rec, int(0.66 * 65535))
    under_b = river_sample(rec, int(0.66 * 65535) + int(0.07 * 65535))
    karst = solid(
        "karst_ridge",
        (
            (under_a["x"] + under_b["x"]) // 2,
            (under_a["y"] + under_b["y"]) // 2 + 20000,
            (under_a["z"] + under_b["z"]) // 2,
        ),
        (
            abs(under_b["x"] - under_a["x"]) + 100000,
            110000,
            abs(under_b["z"] - under_a["z"]) + 100000,
        ),
        color=0x5E6B63,
    )
    dam_a = river_sample(rec, int(0.90 * 65535))
    dam_b = river_sample(rec, int(0.90 * 65535) + int(0.02 * 65535))
    dam_hold = solid(
        "dam_hold",
        ((dam_a["x"] + dam_b["x"]) // 2, (dam_a["y"] + dam_b["y"]) // 2 + 6000, (dam_a["z"] + dam_b["z"]) // 2),
        (60000, 60000, abs(dam_b["z"] - dam_a["z"]) + 50000),
        color=0x8C8C94,
    )
    wetland = vista(
        "wetland",
        bank_point(rec, 36400, -(BANK // 2), 1000),
        (160000, 300, 220000),
        color=0x5F7E8C,
    )
    plains = vista("plains", [down[0], 50000, 700000], (400000, 300, 380000), color=0x8FA05E)
    forest = vista(
        "forest",
        [ruin["position"][0] + 30000, ruin["position"][1], ruin["position"][2] - 20000],
        (300000, 400, 260000),
        color=0x3F6B3F,
    )
    cave = {
        "name": "cave",
        "level": "room",
        "shape": "room",
        "tags": ["Collision.Solid"],
        "position": [mx - 150000, 90000, -565000],
        "size": [16000, 5000, 16000],
        "color": 0x4A4A52,
    }
    modules = [
        mountain,
        shoulder_e,
        shoulder_w,
        watershed,
        cave,
        karst,
        dam_hold,
        plains,
        forest,
        wetland,
        ruin,
        plane("high_gate", gate, (140000, 400, 140000), level="settlement", color=0xC9B27A),
        plane("trail_e0", east[0], level="structure", color=0x8B7355),
        plane("trail_e1", east[1], level="structure", color=0x8B7355),
        plane("trail_e2", east[2], level="structure", color=0x8B7355),
        plane("trail_e3", east[3], level="structure", color=0x8B7355),
        plane("trail_e4", east[4], level="structure", color=0x8B7355),
        plane("trail_ford_e", e_ford, level="structure", color=0x8B7355),
        plane("trail_ford_w", w_ford, level="structure", color=0x8B7355),
        plane("trail_w1", w1, level="structure", color=0x8B7355),
        plane("ford_haven", haven, (140000, 400, 140000), level="settlement", color=0xC9B27A),
    ]
    links = [
        ["high_gate", "trail_e0", "walk"],
        ["watershed", "trail_e0", "walk"],
        ["trail_e0", "trail_e1", "walk"],
        ["trail_e1", "trail_e2", "walk"],
        ["trail_e2", "trail_e3", "walk"],
        ["trail_e3", "trail_e4", "walk"],
        ["trail_e4", "trail_ford_e", "walk"],
        ["trail_ford_e", "trail_ford_w", "walk"],  # the derived ford
        ["trail_ford_w", "trail_w1", "walk"],
        ["trail_w1", "ford_haven", "walk"],
        ["ruin", "trail_e2", "walk"],
    ]
    # --- regional reservoirs, DERIVED from the same geography (mission
    # resource rules): massif spoil (the mountain union the cave carved
    # into its foot), the lake country water field (the lake reach strip
    # union the wetland habitat), forest biomass, and the karst Phos lode.
    # Rates are base recovery per in-game day; every stock starts full so
    # the committed condition is the declared geography. ---
    def solid_box(m):
        p, s = m["position"], m["size"]
        return [p[0] - s[0] // 2, p[1] - s[1], p[2] - s[2] // 2], [p[0] + s[0] // 2, p[1], p[2] + s[2] // 2]

    def union(a, b):
        return [min(x, y) for x, y in zip(a[0], b[0])], [max(x, y) for x, y in zip(a[1], b[1])]

    mlo, mhi = union(solid_box(mountain), solid_box(cave))
    klo, khi = solid_box(karst)
    f = forest["position"]
    flo, fhi = [f[0] - 150000, f[1] - 200, f[2] - 130000], [f[0] + 150000, f[1] + 200, f[2] + 130000]
    lake_t0, lake_t1 = int(0.50 * 65535), int(0.50 * 65535) + int(0.11 * 65535)
    lats = [river_sample(rec, t) for t in range(lake_t0, lake_t1 + 1, 512)]
    llo = [min(s["x"] for s in lats) - 30000, min(s["y"] for s in lats) - 2000, min(s["z"] for s in lats) - 30000]
    lhi = [max(s["x"] for s in lats) + 30000, max(s["y"] for s in lats) + 40000, max(s["z"] for s in lats) + 30000]
    llo, lhi = union((llo, lhi), solid_box(wetland))
    reservoirs = [
        {"name": "massif_spoil", "key": 201, "kind": "terrain", "zone": 2, "lo": mlo, "hi": mhi, "capacity": 400000, "level": 400000, "rate": 2500},
        {"name": "lake_country", "key": 202, "kind": "water", "zone": 1, "lo": llo, "hi": lhi, "capacity": 300000, "level": 300000, "rate": 12000},
        {"name": "forest_biomass", "key": 203, "kind": "biomass", "zone": 1, "lo": flo, "hi": fhi, "capacity": 200000, "level": 200000, "rate": 15000},
        {"name": "karst_phos", "key": 204, "kind": "phos", "zone": 3, "lo": list(klo), "hi": list(khi), "capacity": 160000, "level": 160000, "rate": 8000},
    ]
    src = {
        "schema": 1,
        "generator": 1,
        "ancestry": [0x51AC0, 0x9C0D, 0x2026, 0xEC0],
        "seed": seed & 0xFFFFFFFF,
        "epoch": 1,
        "revision": variant,
        "name": "cascade_macro",
        "modules": modules,
        "links": links,
        "features": [river_src],
        "reservoirs": reservoirs,
    }
    for i, m in enumerate(modules):
        m.setdefault("key", 100 + i)
    # Adaptive trail surfaces: a waypoint plane never reaches past 30% of
    # its shortest walk edge, so consecutive planes cannot overlap and every
    # edge keeps a non-degenerate corridor ramp between the endpoint
    # surfaces. The engine blends the gap between the planes; overlapping
    # banked planes would instead present their full height difference as a
    # single cliff at the higher plane's boundary.
    pmap = {m["name"]: m["position"] for m in modules}
    minlen = {}
    for a, b, kind in links:
        if kind != "walk":
            continue
        pa, pb = pmap[a], pmap[b]
        d = math.isqrt((pb[0] - pa[0]) ** 2 + (pb[2] - pa[2]) ** 2)
        for n in (a, b):
            minlen[n] = min(minlen.get(n, d), d)
    for m in modules:
        if m["name"] in minlen and m["shape"] == "plane" and "Surface.Walk" in m.get("tags", []):
            h = min(40000, int(minlen[m["name"]] * 0.3))
            m["size"] = [2 * h, 400, 2 * h]
    return src, rec, ford_t


def check_cost_case(src, rec):
    """The mission's route-cost case: from the ruin, the geometrically
    nearest settlement is NOT the cheapest reachable destination."""
    modules, links, features, _ = runtime_view(src)
    ids = {m["name"]: i for i, m in enumerate(src["modules"])}
    ruin = ids["ruin"]
    gate = ids["high_gate"]
    haven = ids["ford_haven"]
    rg = route_cost(modules, links, features, ruin, gate, CAP_WALK)
    rh = route_cost(modules, links, features, ruin, haven, CAP_WALK)
    assert rg and rh, "both settlements must be reachable from the ruin"
    M = modules
    dg = math.dist(M[ruin][:1] + M[ruin][2:], M[gate][:1] + M[gate][2:])
    dh = math.dist(M[ruin][:1] + M[ruin][2:], M[haven][:1] + M[haven][2:])
    return {
        "nearest": "high_gate" if dg < dh else "ford_haven",
        "cheapest": "high_gate" if rg[0] < rh[0] else "ford_haven",
        "distance_to_high_gate": round(dg),
        "distance_to_ford_haven": round(dh),
        "cost_to_high_gate": rg[0],
        "cost_to_ford_haven": rh[0],
        "path_to_high_gate": rg[1],
        "path_to_ford_haven": rh[1],
        "holds": dg < dh and rh[0] < rg[0],
    }


def build(seed=SHOWCASE):
    """Generate, compile, and re-derive with retries until the world is
    realizable and the route-cost case holds. Fully deterministic."""
    errors = []
    for variant in range(24):
        src, rec, ford_t = generate(seed, variant)
        try:
            compile_recipe(src)
        except (ValueError, KeyError) as e:
            errors.append(f"variant {variant}: {e}")
            continue
        case = check_cost_case(src, rec)
        if not case["holds"]:
            errors.append(f"variant {variant}: cost case {case['nearest']}/{case['cheapest']}")
            continue
        data, manifest = compile_recipe(src)
        return {
            "src": src,
            "rec": rec,
            "ford_t": ford_t,
            "data": data,
            "manifest": manifest,
            "cost_case": case,
            "variants_tried": variant + 1,
            "rejected_variants": errors,
        }
    raise AssertionError(f"no realizable macro geography for seed {seed:#x}: {errors}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=lambda x: int(x, 0), default=SHOWCASE)
    ap.add_argument("--source", default=str(ROOT / "content/worlds/macro.json"))
    ap.add_argument("--output", default=str(ROOT / "build/macro.cws"))
    ap.add_argument("--c-include", default=str(ROOT / "content/worlds/macro.inc"))
    ap.add_argument("--summary", default=str(ROOT / "results/worldsdk/macro-summary.json"))
    args = ap.parse_args()
    built = build(args.seed)
    src, data, manifest, case = built["src"], built["data"], built["manifest"], built["cost_case"]
    for p in (args.source, args.output, args.c_include, args.summary):
        pathlib.Path(p).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(args.source).write_text(json.dumps(src, indent=2) + "\n")
    pathlib.Path(args.output).write_bytes(data)
    pathlib.Path(args.c_include).write_text(
        "/* Generated by tools/worldsdk/macro.py. Do not edit. */\n"
        "static const unsigned char ws_macro_product[]={\n"
        + "".join(
            ",".join(map(str, data[i : i + 24])) + ",\n"
            for i in range(0, len(data), 24)
        )
        + "};\n"
    )
    result = {
        "ok": True,
        "seed": args.seed,
        "variants_tried": built["variants_tried"],
        "product_sha256": manifest["sha256"],
        "bytes": manifest["bytes"],
        "schema": manifest["schema"],
        "modules": len(src["modules"]),
        "links": len(src["links"]),
        "features": manifest["features"],
        "exceptions": manifest["exceptions"],
        "reservoirs": [
            {"name": v["name"], "kind": v["kind"], "zone": v["zone"], "capacity": v["capacity"], "level": v["level"], "rate": v["rate"]}
            for v in src["reservoirs"]
        ],
        "ford_t": built["ford_t"],
        "cost_case": case,
        "hierarchy": [
            "mountain",
            "valley",
            "plains",
            "watershed/river",
            "lake/wetland",
            "forest",
            "cave",
            "ruin",
            "two settlements",
            "derived wilderness route",
        ],
    }
    pathlib.Path(args.summary).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
