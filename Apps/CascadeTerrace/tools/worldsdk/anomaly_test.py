#!/usr/bin/env python3
"""Gate 4 test (nonlocal Phos topology). Proves, against the C engine
through libsdk.so, with an independent Python mirror of every rule:
  - the committed macro artifacts are exactly the generator output, schema 4 (carrying the schema 3 reservoir section)
    with the phos_ruin gate seat on the karst ridge top, the winze lift and
    the anchored anomaly gate matching the C-loaded link records;
  - the gate seat derives from the karst ridge record (river-derived, like
    every other anchor), stands inside the anchored Phos region, with the
    far end outside it and conventionally nonadjacent;
  - openness, gate tolls and state-aware routing are bit-exact between C and
    Python: the boundary is exactly half the anchored stock, the open gate
    halves the ruin->haven journey, and closing it restores the ordinary
    route and cost bit for bit (the Gate 2 literals);
  - traversal is state-gated and instant in both directions, falls back to
    the ordinary lift at the shared seat when closed, locks out while
    traveling, and the destination is reconstructed from local records;
  - the depletion proof runs through the C op path with a synced mirror:
    draw to the exact boundary, one unit below closes the gate, the ledger
    identity holds after every op, deterministic recharge reopens it;
  - the state wire roundtrip carries the deciding levels, so gate state
    survives encode/decode and save/restore;
  - purpose-built probe products prove the compiler rules: a gate whose seat
    has no ordinary path at all is legitimate content, while far ends inside
    the anchored region, adjacent ends, duplicate anchors, non-Phos anchors
    and seats outside the region all reject fail-closed;
  - additional seeds generalize the whole proof.
Standard library only."""

import ctypes as C
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import Pos, Recipe, ROOT
from server import Address, Context, Disposition, Operation, State, Traveler, library
from sdk import (
    ANOMALY_COST,
    CAP_ANOMALY,
    CAP_LIFT,
    CAP_PORTAL,
    CAP_WALK,
    RESERVOIR_KINDS,
    compile_recipe,
    ledger_check,
    link_cost,
    link_open,
    resources_tick,
    route_cost,
    route_cost_state,
    runtime_view,
)
import macro

SEED = macro.SHOWCASE
OK = 0
EXCAVATE, CONVERT, SPEND = 11, 12, 13
R_SPOIL, R_LAKE, R_FOREST, R_KARST = 0, 1, 2, 3
SITE = 1


def harness(lib):
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_init.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_join.argtypes = [C.POINTER(State), C.c_uint32]
    lib.ws_apply.argtypes = [C.POINTER(State), C.POINTER(Recipe), Context, Operation]
    lib.ws_apply.restype = Disposition
    lib.ws_state_validate.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_state_validate.restype = C.c_int
    lib.ws_state_hash.argtypes = [C.POINTER(State)]
    lib.ws_state_hash.restype = C.c_uint32
    lib.ws_state_encode.argtypes = [C.POINTER(State), C.c_void_p, C.c_size_t]
    lib.ws_state_encode.restype = C.c_size_t
    lib.ws_state_decode.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_decode.restype = C.c_int
    lib.ws_link_open.argtypes = [C.POINTER(Recipe), C.POINTER(State), C.c_uint16]
    lib.ws_link_open.restype = C.c_int
    lib.ws_route_cost.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.c_uint32, C.POINTER(C.c_uint64), C.POINTER(C.c_uint16), C.c_size_t]
    lib.ws_route_cost.restype = C.c_int
    lib.ws_route_cost_state.argtypes = [C.POINTER(Recipe), C.POINTER(State), C.c_uint16, C.c_uint16, C.c_uint32, C.POINTER(C.c_uint64), C.POINTER(C.c_uint16), C.c_size_t]
    lib.ws_route_cost_state.restype = C.c_int
    lib.ws_link_cost.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint32]
    lib.ws_link_cost.restype = C.c_uint64
    lib.ws_use_link.argtypes = [C.POINTER(Recipe), C.POINTER(Traveler)]
    lib.ws_use_link.restype = C.c_int
    lib.ws_use_link_state.argtypes = [C.POINTER(Recipe), C.POINTER(State), C.POINTER(Traveler)]
    lib.ws_use_link_state.restype = C.c_int
    lib.ws_resources_tick.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_uint32]
    lib.ws_ledger_check.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_ledger_check.restype = C.c_int
    lib.ws_save.argtypes = [C.POINTER(State), C.c_char_p]
    lib.ws_save.restype = C.c_int
    lib.ws_restore.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_char_p]
    lib.ws_restore.restype = C.c_int


def mirror_state(recs):
    return {
        "clock_s": 0,
        "level": [v["level"] for v in recs],
        "material": [0] * 8,
        "shards": [0] * 8,
        "sites": [],
        "recovered": 0,
        "used": 0,
        "lost": 0,
        "player_count": 1,
    }


def main():
    lib = library()
    harness(lib)
    results = {"stage": "anomaly-topology", "checks": 0}

    def check(cond, label):
        results["checks"] += 1
        assert cond, label

    # 1. Committed artifacts are exactly the generator output.
    built = macro.build(SEED)
    src, data, manifest = built["src"], built["data"], built["manifest"]
    check(manifest["schema"] == 4, "committed product is not schema 4")
    check(len(src["modules"]) == 22 and len(src["links"]) == 13, "product shape")
    check(manifest["bytes"] == 2196, "product bytes")
    check(src == json.loads((ROOT / "content/worlds/macro.json").read_text()), "committed macro.json drift")
    check(data == (ROOT / "build/macro.cws").read_bytes(), "committed product bytes drift")
    inc = (ROOT / "content/worlds/macro.inc").read_text()
    inc_bytes = bytes(int(v) for v in re.findall(r"\d+", inc.split("{", 1)[1]))
    check(inc_bytes == data, "committed macro.inc drift")
    r = Recipe()
    check(lib.ws_load(C.byref(r), data, len(data)) == 0, "C rejects the macro product")
    ids = {m["name"]: i for i, m in enumerate(src["modules"])}
    RUIN, GATE, HAVEN, E3, PHOS = ids["ruin"], ids["high_gate"], ids["ford_haven"], ids["trail_e3"], ids["phos_ruin"]
    modules, links, features, recs = runtime_view(src)
    gate = next(i for i, l in enumerate(links) if l[2] == 3)
    lift = next(i for i, l in enumerate(links) if l[2] == 1)
    check((r.links[gate].a, r.links[gate].b, r.links[gate].kind, r.links[gate].reserved) == (PHOS, RUIN, 3, R_KARST), "gate record drift")
    check((r.links[lift].a, r.links[lift].b, r.links[lift].kind) == (E3, PHOS, 1), "lift record drift")
    check(links[gate] == (PHOS, RUIN, 3, R_KARST) and links[lift][2] == 1, "runtime view links drift")

    # 2. The gate seat derives from the karst ridge record and stands inside
    # the anchored region; the far end stands outside it.
    by_name = {m["name"]: m for m in src["modules"]}
    u0 = macro.river_sample(built["rec"], int(0.66 * 65535))
    u1 = macro.river_sample(built["rec"], int(0.66 * 65535) + int(0.07 * 65535))
    karst_pos = [(u0["x"] + u1["x"]) // 2, (u0["y"] + u1["y"]) // 2 + 20000, (u0["z"] + u1["z"]) // 2]
    seat_rule = [karst_pos[0] + macro.wob(SEED, 10000, 0xC7), karst_pos[1], karst_pos[2] + macro.wob(SEED, 10000, 0xC8)]
    check(by_name["phos_ruin"]["position"] == seat_rule, "phos_ruin not ridge-derived")
    check(by_name["phos_ruin"]["phos"] == "Geo", "phos_ruin is not a Phos ruin")
    v = recs[R_KARST]
    A, B = modules[PHOS], modules[RUIN]
    check(v["lo"][0] <= A[0] <= v["hi"][0] and v["lo"][2] <= A[2] <= v["hi"][2], "seat outside the anchored region")
    check(not (v["lo"][0] <= B[0] <= v["hi"][0] and v["lo"][2] <= B[2] <= v["hi"][2]), "far end inside the anchored region")
    check((A[0], A[1], A[2]) == (-27171, 164282, 474302), "seat literal drift")
    check((B[0], B[1], B[2]) == (-208672, 244567, 38666), "far end literal drift")

    # 3. Openness parity: exact boundary at half the anchored stock.
    s = State()
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    for lvl in list(range(0, 160001, 1000)) + [79999, 80000, 79998, 145534, 28930, 111999]:
        s.level[R_KARST] = lvl
        st = {"level": list(s.level[:4])}
        check(lib.ws_link_open(C.byref(r), C.byref(s), gate) == link_open(links, recs, st, gate), f"open parity {lvl}")
    s.level[R_KARST] = 79999
    check(lib.ws_link_open(C.byref(r), C.byref(s), gate) == 0 and s.level[R_KARST] == 79999, "closed at 79999")
    s.level[R_KARST] = 80000
    check(lib.ws_link_open(C.byref(r), C.byref(s), gate) == 1, "open at 80000")
    check(lib.ws_link_open(C.byref(r), None, gate) == 1, "declared levels with no state")
    check(link_open(links, recs, None, gate) == 1, "mirror declared levels")
    for i in (lift, 0, len(links)):
        check(lib.ws_link_open(C.byref(r), C.byref(s), i) == 0, f"non-gate {i} never open")
        check(link_open(links, recs, {"level": list(s.level[:4])}, i if i < len(links) else 0) == 0, f"mirror non-gate {i}")

    # 4. Toll parity: the gate toll is separate from walk, lift and portal.
    for caps in (CAP_WALK, CAP_WALK | CAP_LIFT, CAP_WALK | CAP_ANOMALY, CAP_WALK | CAP_LIFT | CAP_ANOMALY, CAP_WALK | CAP_PORTAL):
        for i in range(len(links)):
            c_cost = lib.ws_link_cost(C.byref(r), i, caps)
            p_cost = link_cost(modules, links, features, i, caps)
            check(c_cost == (p_cost if p_cost is not None else 2**64 - 1), f"toll parity link {i} caps {caps}")
    check(link_cost(modules, links, features, gate, CAP_ANOMALY) == ANOMALY_COST, "gate toll literal")
    check(link_cost(modules, links, features, lift, CAP_WALK | CAP_LIFT) == 3007, "lift toll literal")

    # 5. Routing parity and exact literals, stateless and state-aware.
    all_caps = CAP_WALK | CAP_LIFT | CAP_ANOMALY

    def c_route(a, b, caps, state=None):
        cost = C.c_uint64()
        path = (C.c_uint16 * 128)()
        if state is None:
            n = lib.ws_route_cost(C.byref(r), a, b, caps, C.byref(cost), path, 128)
        else:
            n = lib.ws_route_cost_state(C.byref(r), C.byref(state), a, b, caps, C.byref(cost), path, 128)
        return (n, cost.value, list(path[:n])) if n > 0 else None

    for caps in (CAP_WALK, all_caps):
        for a, b in ((RUIN, GATE), (RUIN, HAVEN), (GATE, HAVEN)):
            c = c_route(a, b, caps)
            p = route_cost(modules, links, features, a, b, caps)
            check(c is not None and p is not None, f"stateless route {a}->{b} caps {caps} unreachable")
            check(c[1] == p[0] and c[2] == p[1], f"stateless route parity {a}->{b} caps {caps}")
    check(c_route(RUIN, GATE, CAP_WALK)[1] == 3246425, "ordinary climb literal")
    check(c_route(RUIN, HAVEN, CAP_WALK)[1] == 2668102, "ordinary haven literal")
    for lvl in (160000, 80000, 79999, 28930, 0):
        s.level[R_KARST] = lvl
        st = {"level": list(s.level[:4])}
        for a, b in ((RUIN, PHOS), (PHOS, RUIN), (RUIN, HAVEN), (HAVEN, PHOS), (RUIN, GATE)):
            c = c_route(a, b, all_caps, state=s)
            p = route_cost_state(modules, links, features, recs, st, a, b, all_caps)
            check((c is None) == (p is None), f"route presence parity {a}->{b} lvl {lvl}")
            if p is not None:
                check(c[1] == p[0] and c[2] == p[1], f"route parity {a}->{b} lvl {lvl}")
        c = c_route(RUIN, PHOS, CAP_WALK, state=s)
        check(c is None, f"walk-only never crosses lvl {lvl}")
    s.level[R_KARST] = 160000
    check(c_route(RUIN, PHOS, all_caps, state=s) == (2, 1500, [RUIN, PHOS]), "open crossing literal")
    check(c_route(RUIN, HAVEN, all_caps, state=s)[1] == 1545498, "open haven shortcut literal")
    s.level[R_KARST] = 79999
    check(c_route(RUIN, PHOS, all_caps, state=s) == (4, 1130118, [RUIN, 14, E3, PHOS]), "closed ordinary route literal")
    check(c_route(RUIN, HAVEN, all_caps, state=s) == (8, 2668102, [RUIN, 14, E3, 16, 17, 18, 19, HAVEN]), "closed haven restoration literal")
    check(c_route(RUIN, GATE, all_caps, state=s)[1] == 3246425, "closed climb unchanged literal")
    s.level[R_KARST] = 160000

    # 6. Traversal parity: state-gated, instant, local, with fallback.
    def seat(i):
        x, y, z = modules[i]
        return Traveler(Address(Pos(x, y, z), 0xFFFF))

    def at_tuple(t):
        return (t.at.pos.x, t.at.pos.y, t.at.pos.z, t.at.scope)

    s.level[R_KARST] = 160000
    t = seat(PHOS)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 1, "open crossing from the seat")
    check(at_tuple(t) == (-208672, 244567, 38666, 0xFFFF), "arrival at the far end")
    t = seat(RUIN)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 1, "open crossing from the far end")
    check(at_tuple(t) == (-27171, 164282, 474302, 0xFFFF), "arrival at the seat")
    s.level[R_KARST] = 79999
    t = seat(RUIN)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 0, "closed gate is absent at the far end")
    check(at_tuple(t) == (-208672, 244567, 38666, 0xFFFF), "failed use leaves the traveler untouched")
    t = seat(PHOS)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 1, "closed gate falls back to the lift")
    check((t.destination.pos.x, t.destination.pos.y, t.destination.pos.z) == (-149969, 159267, 454026), "lift destination")
    check(t.remaining == 2508, "lift travel time")
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 0, "traveling locks out use")
    t = seat(PHOS)
    check(lib.ws_use_link(C.byref(r), C.byref(t)) == 1, "stateless scan still serves the lift")
    check((t.destination.pos.x, t.remaining) == (-149969, 2508), "stateless lift unchanged")
    t = seat(RUIN)
    check(lib.ws_use_link(C.byref(r), C.byref(t)) == 0, "stateless scan never crosses gates")
    t = seat(RUIN)
    check(lib.ws_use_link_state(C.byref(r), None, C.byref(t)) == 1, "declared levels with no state")
    check(at_tuple(t) == (-27171, 164282, 474302, 0xFFFF), "view-semantics arrival")

    # 7. The depletion proof through the C op path, mirror synced.
    def apply_op(action, target, seq, amount, aux, at, server=0):
        op = Operation(seq, 1, s.revision, action, target, amount, aux)
        return lib.ws_apply(C.byref(s), C.byref(r), Context(0x5151, Address(Pos(*at), 0xFFFF), server), op)

    def sync(label):
        check(list(s.level[:4]) == m["level"][:4], f"{label}: levels")
        check(list(s.material[:1]) == m["material"][:1], f"{label}: material")
        check(list(s.shards[:1]) == m["shards"][:1], f"{label}: shards")
        check((s.recovered_total, s.used_total, s.lost_total) == (m["recovered"], m["used"], m["lost"]), f"{label}: ledger totals")
        check(s.site_count == len(m["sites"]), f"{label}: site count")
        check(lib.ws_ledger_check(C.byref(s), C.byref(r)) == 1 and ledger_check(m, recs), f"{label}: ledger identity")
        check(lib.ws_state_validate(C.byref(s), C.byref(r)) == 0, f"{label}: state valid")

    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    seat_pos = (-27171, 164282, 474302)
    st = {"level": list(s.level[:4])}
    check(link_open(links, recs, st, gate) == 1, "mirror open at declared")
    check(apply_op(EXCAVATE, R_KARST, 1, 65535, 1, seat_pos).status == OK, "draw 65535")
    m["level"][R_KARST] -= 65535
    m["shards"][0] += 65535
    m["sites"].append({"pos": list(seat_pos), "reservoir": R_KARST, "kind": SITE, "extent": 65535, "amount": 65535})
    sync("draw 65535")
    st = {"level": list(s.level[:4])}
    check(link_open(links, recs, st, gate) == 1, "still open above half")
    check(apply_op(CONVERT, R_KARST, 2, 65535, 1, seat_pos).status == OK, "deposit 2:1")
    m["shards"][0] -= 65535
    m["material"][0] += 32767
    m["lost"] += 32768
    sync("deposit")
    check(apply_op(EXCAVATE, R_KARST, 3, 14465, 1, seat_pos).status == OK, "draw to the boundary")
    m["level"][R_KARST] -= 14465
    m["shards"][0] += 14465
    m["sites"].append({"pos": list(seat_pos), "reservoir": R_KARST, "kind": SITE, "extent": 14465, "amount": 14465})
    sync("boundary draw")
    st = {"level": list(s.level[:4])}
    check(s.level[R_KARST] == 80000 and link_open(links, recs, st, gate) == 1, "boundary holds")
    check(apply_op(EXCAVATE, R_KARST, 4, 1, 1, seat_pos).status == OK, "one unit below")
    m["level"][R_KARST] -= 1
    m["shards"][0] += 1
    m["sites"].append({"pos": list(seat_pos), "reservoir": R_KARST, "kind": SITE, "extent": 1, "amount": 1})
    sync("closure draw")
    st = {"level": list(s.level[:4])}
    check(s.level[R_KARST] == 79999 and link_open(links, recs, st, gate) == 0, "closed one unit below half")
    t = seat(RUIN)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 0, "crossing denied after depletion")
    c = c_route(RUIN, HAVEN, all_caps, state=s)
    check(c == (8, 2668102, [RUIN, 14, E3, 16, 17, 18, 19, HAVEN]), "ordinary geography restored")
    # recharge: day 1 rain refills past the boundary and the gate reopens
    lib.ws_resources_tick(C.byref(s), C.byref(r), 86400)
    resources_tick(m, recs, src["seed"], 86400)
    sync("recharge day 1")
    st = {"level": list(s.level[:4])}
    check(s.level[R_KARST] == 111999 and link_open(links, recs, st, gate) == 1, "reopened by recharge")
    t = seat(RUIN)
    check(lib.ws_use_link_state(C.byref(r), C.byref(s), C.byref(t)) == 1, "crossing after recharge")
    check(at_tuple(t) == (-27171, 164282, 474302, 0xFFFF), "reopened arrival")

    # 8. Wire roundtrip carries the deciding levels.
    buf = (C.c_ubyte * 12000)()
    n = lib.ws_state_encode(C.byref(s), buf, 12000)
    base = 48 + 22 * 28 + 538 + 12 + 4 * 16 + 4 * 20
    check(n == base + 8 + 16 + 4 + 3 * 24 + 24 and buf[4] == 2, "wire v2 layout")
    t2 = State()
    check(lib.ws_state_decode(C.byref(t2), C.byref(r), buf, n) == 0, "decode")
    check(lib.ws_state_hash(C.byref(t2)) == lib.ws_state_hash(C.byref(s)), "hash identity")
    check(t2.level[R_KARST] == 111999 and lib.ws_link_open(C.byref(r), C.byref(t2), gate) == 1, "open state restored open")
    check(lib.ws_state_decode(C.byref(t2), C.byref(r), buf, n - 1) != 0, "truncation rejected")
    t2.level[R_KARST] = 79999
    check(lib.ws_link_open(C.byref(r), C.byref(t2), gate) == 0, "closed levels restored closed")
    check(lib.ws_save(C.byref(s), b"build/anomaly-state-py") == 1, "save")
    u = State()
    check(lib.ws_restore(C.byref(u), C.byref(r), b"build/anomaly-state-py") == 1, "restore")
    check(lib.ws_state_hash(C.byref(u)) == lib.ws_state_hash(C.byref(s)), "save/restore identity")
    check(lib.ws_link_open(C.byref(r), C.byref(u), gate) == 1, "gate state survives save/restore")

    # 9. Purpose-built probe products: legitimate gate-only seats and the
    # fail-closed compiler rules. The lode box is [0..40000]^2 in x/z; the
    # seat stands inside it, the ground plane outside.
    def walk_plane(name, key, position):
        return {"name": name, "key": key, "level": "structure", "shape": "plane", "tags": ["Surface.Walk"], "position": list(position), "size": [20000, 400, 20000]}

    def probe_source(case="gate_only", anchor_kind="phos"):
        seat = walk_plane("seat", 3, (10000, 1000, 10000))  # inside the lode
        ground = walk_plane("ground", 1, (100000, 1000, 0))  # outside it
        mid = walk_plane("mid", 2, (70000, 1000, 0))
        inside2 = walk_plane("inside2", 4, (12000, 1000, 12000))  # inside it
        away2 = walk_plane("away2", 5, (300000, 1000, 0))  # far outside
        shapes = {
            "gate_only": ([ground, seat], [["seat", "ground", "anomaly", "lode"]]),
            "adjacent": ([ground, mid, seat], [["ground", "mid", "walk"], ["mid", "seat", "walk"], ["seat", "ground", "anomaly", "lode"]]),
            "far_inside": ([ground, seat, inside2], [["ground", "inside2", "walk"], ["seat", "inside2", "anomaly", "lode"]]),
            "duplicate": ([ground, seat, away2], [["seat", "ground", "anomaly", "lode"], ["seat", "away2", "anomaly", "lode"]]),
        }
        modules_, links = shapes[case]
        return {
            "schema": 1, "generator": 1, "ancestry": [7, 7, 7, 7], "seed": 1234,
            "epoch": 1, "revision": 0, "name": "gate_probe",
            "modules": modules_,
            "links": links,
            "reservoirs": [
                {"name": "lode", "key": 201, "kind": anchor_kind, "zone": 0, "lo": [0, 0, 0], "hi": [40000, 10000, 40000], "capacity": 1000, "level": 1000, "rate": 10},
                {"name": "far_water", "key": 202, "kind": "water", "zone": 0, "lo": [200000, 0, 0], "hi": [400000, 10000, 40000], "capacity": 1000, "level": 1000, "rate": 10},
            ],
        }

    # legitimate: the seat has no ordinary path at all (gate-only content)
    pdata, _ = compile_recipe(probe_source())
    pr = Recipe()
    check(lib.ws_load(C.byref(pr), pdata, len(pdata)) == 0, "gate-only seat product loads")
    ps = State()
    lib.ws_state_init(C.byref(ps), C.byref(pr))
    lib.ws_join(C.byref(ps), 0x5151)
    gidx = next(i for i in range(pr.links_n) if pr.links[i].kind == 3)
    check(lib.ws_link_open(C.byref(pr), C.byref(ps), gidx) == 1, "probe gate open at declared")
    ps.level[0] = 499
    check(lib.ws_link_open(C.byref(pr), C.byref(ps), gidx) == 0, "probe gate closed below half")
    pmodules, plinks, _, precs = runtime_view(probe_source())
    check(link_open(plinks, precs, {"level": [1000, 1000]}, gidx) == 1, "probe mirror parity")
    check(link_open(plinks, precs, {"level": [499, 1000]}, gidx) == 0, "probe mirror closure")

    def rejects(case, label, **kw):
        st = probe_source(case, **kw)
        try:
            compile_recipe(st)
        except ValueError:
            check(True, label)
        else:
            raise AssertionError(f"compiler accepted {label}")

    rejects("far_inside", "far end inside the anchored region")
    rejects("adjacent", "conventionally adjacent ends")
    rejects("duplicate", "duplicate anchor")
    rejects("gate_only", "anchor not a Phos region", anchor_kind="water")

    # 10. Generalization: more seeds carry the same gate guarantees.
    results["generalized_seeds"] = []
    for seed in (1, 3735928559, 20260922):
        gen = macro.build(seed)
        gsrc, gdata = gen["src"], gen["data"]
        gr = Recipe()
        check(lib.ws_load(C.byref(gr), gdata, len(gdata)) == 0, f"seed {seed:#x} product rejected")
        gmodules, glinks, gfeatures, grecs = runtime_view(gsrc)
        gids = {mm["name"]: i for i, mm in enumerate(gsrc["modules"])}
        ggate = next(i for i, l in enumerate(glinks) if l[2] == 3)
        anchor = glinks[ggate][3]
        gv = grecs[anchor]
        check(gv["kind"] == RESERVOIR_KINDS["phos"], f"seed {seed:#x} anchor is Phos")
        check(gv["lo"][0] <= gmodules[glinks[ggate][0]][0] <= gv["hi"][0] and gv["lo"][2] <= gmodules[glinks[ggate][0]][2] <= gv["hi"][2], f"seed {seed:#x} seat inside")
        gs = State()
        lib.ws_state_init(C.byref(gs), C.byref(gr))
        lib.ws_join(C.byref(gs), 0x5151)
        check(lib.ws_link_open(C.byref(gr), C.byref(gs), ggate) == 1, f"seed {seed:#x} open at declared")
        gs.level[anchor] = gv["capacity"] // 2
        check(lib.ws_link_open(C.byref(gr), C.byref(gs), ggate) == 1, f"seed {seed:#x} half holds")
        gs.level[anchor] = gv["capacity"] // 2 - 1
        check(lib.ws_link_open(C.byref(gr), C.byref(gs), ggate) == 0, f"seed {seed:#x} below half closes")
        gst = {"level": list(gs.level[: len(grecs)])}
        check(link_open(glinks, grecs, gst, ggate) == 0, f"seed {seed:#x} mirror closure")
        ruin_i, haven_i = gids["ruin"], gids["ford_haven"]
        cost = C.c_uint64()
        path = (C.c_uint16 * 128)()
        n = lib.ws_route_cost_state(C.byref(gr), C.byref(gs), ruin_i, haven_i, all_caps, C.byref(cost), path, 128)
        p = route_cost_state(gmodules, glinks, gfeatures, grecs, gst, ruin_i, haven_i, all_caps)
        check(n > 0 and p is not None and cost.value == p[0] and list(path[:n]) == p[1], f"seed {seed:#x} closed-route parity")
        results["generalized_seeds"].append({"seed": seed, "bytes": len(gdata), "closed_haven_cost": p[0]})

    (ROOT / "results/worldsdk").mkdir(parents=True, exist_ok=True)
    (ROOT / "results/worldsdk/anomaly-test.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
