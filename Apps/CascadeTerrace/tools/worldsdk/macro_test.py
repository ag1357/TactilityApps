#!/usr/bin/env python3
"""Gate 2 test (mission §49, macro geography). Proves, against the C engine
through libsdk.so:
  - the committed macro artifacts are exactly the generator's output (no
    hand-edited geography drifting from the seed);
  - the §49 hierarchy is present and anchored (mountain, valley, plains,
    watershed/river, lake/wetland, forest, cave, ruin, two settlements);
  - the wilderness route is DERIVED: every route waypoint position equals
    the river-derived formula recomputed here from the committed river
    record alone (perpendicular bank offsets, elevation following the
    water, the ford where the river approaches the ruin);
  - river reconstruction is bit-exact between C and Python across the whole
    curve, and chunk windows agree on every shared boundary sample;
  - typed exceptions (waterfall, rapids, lake, underground, dam) are
    visible in reconstruction;
  - the route-cost case holds: the geometrically nearest settlement is NOT
    the cheapest reachable destination, in both C and Python;
  - every declared walk edge traverses continuously (100 mm swept steps,
    runtime ground and collision, no teleports);
  - the generator generalizes: additional seeds build realizable worlds
    with the same guarantees.
Traversal uses the probe method. Standard library only."""

import ctypes as C
import json
import math
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import ROOT, Module, Pos, Recipe
from sdk import CAP_WALK, feature_record, link_cost, river_sample, river_window, route_cost, runtime_view
from server import Address, library
import macro

SEED = macro.SHOWCASE


class RiverSample(C.Structure):
    _fields_ = [
        ("pos", Pos),
        ("tangent_x", C.c_int32),
        ("tangent_z", C.c_int32),
        ("width", C.c_uint16),
        ("depth", C.c_uint16),
        ("flow", C.c_uint16),
        ("surfaced", C.c_uint16),
    ]


class Port(C.Structure):
    _fields_ = [("pos", Pos), ("owner", C.c_uint16), ("wall", C.c_uint16), ("mode", C.c_uint16), ("width", C.c_uint16)]


def harness(lib):
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_materialize.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Module)]
    lib.ws_move.argtypes = [C.POINTER(Recipe), C.POINTER(Address), C.c_int32, C.c_int32]
    lib.ws_river_sample.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.POINTER(RiverSample)]
    lib.ws_river_sample.restype = C.c_int
    lib.ws_river_window.argtypes = [C.POINTER(Recipe), C.c_uint16, Pos, Pos, C.POINTER(C.c_uint16), C.POINTER(C.c_uint16)]
    lib.ws_river_window.restype = C.c_int
    lib.ws_link_cost.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint32]
    lib.ws_link_cost.restype = C.c_uint64
    lib.ws_route_cost.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.c_uint32, C.POINTER(C.c_uint64), C.POINTER(C.c_uint16), C.c_size_t]
    lib.ws_route_cost.restype = C.c_int
    lib.ws_reachable.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.c_uint32]
    lib.ws_reachable.restype = C.c_int
    lib.ws_ports.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Port), C.c_int]
    lib.ws_ports.restype = C.c_int


def c_river(lib, r, fi, t):
    s = RiverSample()
    assert lib.ws_river_sample(C.byref(r), fi, t, C.byref(s))
    return {
        "x": s.pos.x, "y": s.pos.y, "z": s.pos.z,
        "tangent_x": s.tangent_x, "tangent_z": s.tangent_z,
        "width": s.width, "depth": s.depth, "flow": s.flow, "surfaced": s.surfaced,
    }


def bank_point(rec, t, offset, rise):
    """The published derivation rule, recomputed here from the river record
    alone: offset along the local perpendicular, lifted above the water."""
    s = river_sample(rec, t)
    px, pz = -s["tangent_z"], s["tangent_x"]
    return [s["x"] + px * offset // 65536, s["y"] + rise, s["z"] + pz * offset // 65536]


def walk(lib, r, a, b):
    """Probe method: direct edge, 100 mm swept steps, runtime ground+collision."""
    A, B = Module(), Module()
    lib.ws_materialize(C.byref(r), a, C.byref(A))
    lib.ws_materialize(C.byref(r), b, C.byref(B))
    at = Address(Pos(A.pos.x, A.pos.y + 300, A.pos.z), 65535)
    for step in range(20000):
        dx, dz = B.pos.x - at.pos.x, B.pos.z - at.pos.z
        if math.hypot(dx, dz) < 150:
            return True, step
        m = max(abs(dx), abs(dz))
        if not lib.ws_move(C.byref(r), C.byref(at), int(dx / m * 100), int(dz / m * 100)):
            return False, step
    return False, 20000


def main():
    lib = library()
    harness(lib)
    results = {"stage": "macro-geography", "checks": 0}

    def check(cond, label):
        results["checks"] += 1
        assert cond, label

    # 1. Committed artifacts are exactly the generator output.
    built = macro.build(SEED)
    src, data = built["src"], built["data"]
    committed_src = json.loads((ROOT / "content/worlds/macro.json").read_text())
    check(src == committed_src, "committed macro.json differs from generator output")
    check(data == (ROOT / "build/macro.cws").read_bytes(), "committed product bytes differ")
    inc = (ROOT / "content/worlds/macro.inc").read_text()
    inc_bytes = bytes(int(v) for v in re.findall(r"\d+", inc.split("{", 1)[1]))
    check(inc_bytes == data, "committed macro.inc differs from product bytes")
    again = macro.build(SEED)
    check(again["data"] == data, "generation is not deterministic")

    # 2. Load in C; schema 2 with one river and five typed exceptions.
    r = Recipe()
    check(lib.ws_load(C.byref(r), data, len(data)) == 0, "C rejects the macro product")
    check(r.features_n == 1 and r.exceptions_n == 5, "feature/exception counts")
    ids = {m["name"]: i for i, m in enumerate(src["modules"])}
    rec = feature_record(src["features"][0])

    # 3. The §49 hierarchy is anchored.
    by_name = {m["name"]: m for m in src["modules"]}
    check(by_name["mountain"]["level"] == "region" and "Collision.Solid" in by_name["mountain"]["tags"], "mountain")
    check(by_name["plains"]["level"] == "region", "plains")
    check(src["features"][0]["kind"] == "river", "river")
    check(any(e["type"] == "lake" for e in src["features"][0]["exceptions"]), "lake")
    check(by_name["wetland"]["level"] == "region", "wetland")
    check(by_name["forest"]["level"] == "region", "forest")
    check(by_name["cave"]["shape"] == "room", "cave")
    check("Action.Repair" in by_name["ruin"]["tags"], "ruin")
    settlements = [m for m in src["modules"] if m.get("level") == "settlement"]
    check(len(settlements) == 2, "two settlements")
    # The valley: the derived route descends the watershed to the haven
    # (the ford banks share one elevation: a crossing, not a descent).
    route_names = ["watershed", "trail_e0", "trail_e1", "trail_e2", "trail_e3", "trail_e4", "trail_ford_e", "trail_ford_w", "trail_w1", "ford_haven"]
    ys = [by_name[n]["position"][1] for n in route_names]
    check(all(ys[i] >= ys[i + 1] for i in range(len(ys) - 1)), "route rises along the valley")
    check(ys[0] > ys[-1] and sum(1 for i in range(len(ys) - 1) if ys[i] > ys[i + 1]) >= 8, "route does not descend the valley")
    # The cave keeps a public entrance port.
    ports = (Port * 4)()
    check(lib.ws_ports(C.byref(r), ids["cave"], ports, 4) >= 1, "cave has no entrance port")

    # 4. The route is derived from the river, not hand-positioned: every
    # waypoint equals the bank formula recomputed from the river record.
    for name, t, off, rise in [
        ("trail_e0", macro.EAST_T[0], macro.BANK, macro.RISE),
        ("trail_e1", macro.EAST_T[1], macro.BANK, macro.RISE),
        ("trail_e2", macro.EAST_T[2], macro.BANK, macro.RISE),
        ("trail_e3", macro.EAST_T[3], macro.BANK, macro.RISE),
        ("trail_e4", macro.EAST_T[4], macro.BANK, macro.RISE),
        ("ford_haven", 64500, -(macro.BANK + 60000), macro.RISE + 2000),
        ("wetland", 36400, -(macro.BANK // 2), 1000),
    ]:
        check(by_name[name]["position"] == bank_point(rec, t, off, rise), f"{name} not river-derived")
    # The ford is where the river comes closest to the ruin.
    ruin = by_name["ruin"]["position"]
    ford_t = min(range(macro.FORD_SCAN[0], macro.FORD_SCAN[1] + 1, 128), key=lambda t: (river_sample(rec, t)["x"] - ruin[0]) ** 2 + (river_sample(rec, t)["z"] - ruin[2]) ** 2)
    check(ford_t == built["ford_t"], "ford t mismatch")
    check(by_name["trail_ford_e"]["position"] == bank_point(rec, ford_t, macro.BANK, macro.RISE), "east ford bank not derived")
    check(by_name["trail_ford_w"]["position"] == bank_point(rec, ford_t, -macro.BANK, macro.RISE), "west ford bank not derived")
    # The ruin and the gate derive from the river record and the massif.
    check(by_name["ruin"]["position"] == bank_point(rec, 26000, macro.BANK + 70000 + macro.wob(SEED, 20000, 0xC4), macro.RISE + 4000), "ruin not derived")
    gate_rule = [by_name["shoulder_east"]["position"][0] + macro.wob(SEED, 20000, 0xD5), by_name["shoulder_east"]["position"][1] - 50000, by_name["shoulder_east"]["position"][2] + 220000]
    check(by_name["high_gate"]["position"] == gate_rule, "gate not derived from the massif")
    check(by_name["watershed"]["position"] == [src["features"][0]["upstream"][0], src["features"][0]["upstream"][1] + 2000, src["features"][0]["upstream"][2] + 12000], "watershed not at the col")
    # The karst ridge and dam wall sit over their typed reaches.
    u0, u1 = river_sample(rec, int(0.66 * 65535)), river_sample(rec, int(0.66 * 65535) + int(0.07 * 65535))
    check(by_name["karst_ridge"]["position"] == [(u0["x"] + u1["x"]) // 2, (u0["y"] + u1["y"]) // 2 + 20000, (u0["z"] + u1["z"]) // 2], "karst not over the underground reach")
    d0, d1 = river_sample(rec, int(0.90 * 65535)), river_sample(rec, int(0.90 * 65535) + int(0.02 * 65535))
    check(by_name["dam_hold"]["position"] == [(d0["x"] + d1["x"]) // 2, (d0["y"] + d1["y"]) // 2 + 6000, (d0["z"] + d1["z"]) // 2], "dam not over the dam reach")

    # 5. Bit-exact C/Python river parity across the whole curve.
    deltas = 0
    for t in range(0, 65536, 61):
        cs = c_river(lib, r, 0, t)
        ps = river_sample(rec, t)
        for k in cs:
            check(cs[k] == ps[k], f"river parity t={t} field={k}")
        deltas += 1
    results["river_parity_samples"] = deltas
    # Chunk windows: C and Python agree; quadrant seams are exact; the
    # windows cover every meander node; a far chunk reconstructs nothing.
    quads = [((-1000000, 0, -1000000), (0, 0, 0)), ((0, 0, -1000000), (1000000, 0, 0)), ((-1000000, 0, 0), (0, 0, 1000000)), ((0, 0, 0), (1000000, 0, 1000000))]
    seen = set()
    for lo, hi in quads:
        pw = river_window(rec, list(lo), list(hi))
        t0, t1 = C.c_uint16(), C.c_uint16()
        ok = lib.ws_river_window(C.byref(r), 0, Pos(*lo), Pos(*hi), C.byref(t0), C.byref(t1))
        check(bool(ok) == (pw is not None), "window presence parity")
        if pw:
            check((t0.value, t1.value) == pw, "window range parity")
            seen.update(range(pw[0], pw[1] + 1, 8192 // 8))
    check(len({k * 8192 for k in range(8)} & seen) == 8, "windows do not cover the curve")
    pw = river_window(rec, [400000, 0, 400000], [900000, 0, 900000])
    check(pw is None, "far chunk should reconstruct nothing")

    # 6. Typed exceptions in reconstruction (C values).
    check(c_river(lib, r, 0, 16000)["flow"] == 1, "rapids flow")
    lake = c_river(lib, r, 0, 36000)
    check(lake["width"] == 48000 and lake["depth"] == 2700 and lake["flow"] == 0, "lake reach")
    check(c_river(lib, r, 0, 45000)["surfaced"] == 0, "underground reach")
    check(c_river(lib, r, 0, 3276)["y"] - c_river(lib, r, 0, 3277)["y"] >= 170000, "waterfall step")
    check(c_river(lib, r, 0, 60291)["y"] - c_river(lib, r, 0, 60292)["y"] >= 14000, "dam step")

    # 7. Cost model parity over every link, and the route-cost case.
    modules, links, features = runtime_view(src)
    for i in range(len(links)):
        c_cost = lib.ws_link_cost(C.byref(r), i, CAP_WALK)
        p_cost = link_cost(modules, links, features, i, CAP_WALK)
        check(c_cost == p_cost, f"link cost parity {i}")
    for a_name, b_name in (("ruin", "high_gate"), ("ruin", "ford_haven"), ("high_gate", "ford_haven")):
        a, b = ids[a_name], ids[b_name]
        cost = C.c_uint64()
        path = (C.c_uint16 * 128)()
        n = lib.ws_route_cost(C.byref(r), a, b, CAP_WALK, C.byref(cost), path, 128)
        p = route_cost(modules, links, features, a, b, CAP_WALK)
        check(n > 1 and p is not None, f"route {a_name}->{b_name} unreachable")
        check(cost.value == p[0], f"route cost parity {a_name}->{b_name}")
        check(list(path[:n]) == p[1], f"route path parity {a_name}->{b_name}")
    case = built["cost_case"]
    check(case["holds"], "route-cost case does not hold")
    check(case["nearest"] == "high_gate" and case["cheapest"] == "ford_haven", "cost case roles")
    results["cost_case"] = case

    # 8. Every declared walk edge traverses continuously (probe method).
    edges = []
    for l in r.links[: r.links_n]:
        if l.kind:
            continue
        ok, steps = walk(lib, r, l.a, l.b)
        check(ok, f"edge {l.a}->{l.b} not walkable")
        edges.append({"edge": [l.a, l.b], "pass": ok, "steps": steps})
    results["walked_edges"] = len(edges)

    # 9. The generator generalizes: more seeds build realizable worlds with
    # the same guarantees (compile, C load, parity, walkability, cost case).
    gen = []
    for seed in (0x1, 0xDEADBEEF, 20260922):
        b = macro.build(seed)
        r2 = Recipe()
        check(lib.ws_load(C.byref(r2), b["data"], len(b["data"])) == 0, f"seed {seed:#x} rejected by C")
        rec2 = feature_record(b["src"]["features"][0])
        for t in range(0, 65536, 997):
            cs = c_river(lib, r2, 0, t)
            ps = river_sample(rec2, t)
            check(all(cs[k] == ps[k] for k in cs), f"seed {seed:#x} parity t={t}")
        for l in r2.links[: r2.links_n]:
            if l.kind:
                continue
            ok, _ = walk(lib, r2, l.a, l.b)
            check(ok, f"seed {seed:#x} edge {l.a}->{l.b} not walkable")
        check(b["cost_case"]["holds"], f"seed {seed:#x} cost case")
        gen.append({"seed": seed, "bytes": len(b["data"]), "variants_tried": b["variants_tried"]})
    results["generalized_seeds"] = gen

    out = ROOT / "results/worldsdk/macro-test.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
