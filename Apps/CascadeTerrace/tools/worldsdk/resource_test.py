#!/usr/bin/env python3
"""Gate 3 test (regional resources and ecology). Proves, against the C
engine through libsdk.so, with an independent Python mirror of every rule:
  - the committed macro artifacts are exactly the generator output, schema 4 (carrying the schema 3 reservoir section)
    with four geography-derived reservoirs matching the C-loaded records;
  - weather, river stage, resource ticks (rate-based recovery, weather and
    zone scaling, biomass coupled to the paired Phos stock, capacity-capped
    inflow, pit healing and compaction) and the ledger identity are
    bit-exact between C and Python at every step of the proof scenario;
  - the proof scenario itself: excavation persists (intentional sites never
    heal, pits heal out of the recovering stock), conversion is bounded and
    lossy, water overdraw depletes the lake visibly and rainfall recovers
    it, Phos depletes and recharges, biomass regrows without scarring;
  - resource ops are fail-closed in C (bad targets, dust, replays, locality,
    carry overflow), transactionally;
  - the state wire round trip carries reservoirs, sites and ledger through
    version 2 exactly, and rejects truncation;
  - the compiler rejects invalid reservoir CONTENT fail-closed;
  - a purpose-built coupled source proves the biomass/Phos recovery coupling
    in C, and additional seeds generalize the whole proof.
Standard library only."""

import ctypes as C
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import Pos, Recipe, ROOT
from server import Address, Context, Disposition, Operation, State, library
from sdk import (
    RESERVOIR_KINDS,
    compile_recipe,
    ledger_check,
    resources_tick,
    river_stage,
    runtime_view,
    weather,
)
import macro

SEED = macro.SHOWCASE
OK, FORMAT, VERSION, BOUNDS, DUPLICATE, REFERENCE, DISCONNECTED, DENIED, STALE, FULL = range(10)
EXCAVATE, CONVERT, SPEND = 11, 12, 13
PIT, SITE = 0, 1
R_SPOIL, R_LAKE, R_FOREST, R_KARST = 0, 1, 2, 3
DISCOVERY = 6


# WsRiverSample mirrored explicitly: inline position, tangents, geometry.
class Sample(C.Structure):
    _fields_ = [
        ("x", C.c_int32),
        ("y", C.c_int32),
        ("z", C.c_int32),
        ("tangent_x", C.c_int32),
        ("tangent_z", C.c_int32),
        ("width", C.c_uint16),
        ("depth", C.c_uint16),
        ("flow", C.c_uint16),
        ("surfaced", C.c_uint16),
    ]


def harness(lib):
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_init.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_join.argtypes = [C.POINTER(State), C.c_uint32]
    lib.ws_apply.argtypes = [C.POINTER(State), C.POINTER(Recipe), Context, Operation]
    lib.ws_apply.restype = Disposition
    lib.ws_merge.argtypes = [C.POINTER(State), C.POINTER(State), C.POINTER(Recipe), Context, Operation]
    lib.ws_merge.restype = Disposition
    lib.ws_state_validate.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_state_validate.restype = C.c_int
    lib.ws_state_hash.argtypes = [C.POINTER(State)]
    lib.ws_state_hash.restype = C.c_uint32
    lib.ws_state_encode.argtypes = [C.POINTER(State), C.c_void_p, C.c_size_t]
    lib.ws_state_encode.restype = C.c_size_t
    lib.ws_state_decode.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_decode.restype = C.c_int
    lib.ws_river_sample.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.POINTER(Sample)]
    lib.ws_river_sample.restype = C.c_int
    lib.ws_river_stage.argtypes = [C.POINTER(Recipe), C.POINTER(State), C.c_uint16, C.c_uint16, C.POINTER(Sample)]
    lib.ws_river_stage.restype = C.c_int
    lib.ws_weather.argtypes = [C.POINTER(Recipe), C.c_uint32]
    lib.ws_weather.restype = C.c_uint32
    lib.ws_resources_tick.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_uint32]
    lib.ws_ledger_check.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_ledger_check.restype = C.c_int
    lib.ws_save.argtypes = [C.POINTER(State), C.c_char_p]
    lib.ws_save.restype = C.c_int
    lib.ws_restore.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_char_p]
    lib.ws_restore.restype = C.c_int


def c_sample(lib, r, s, t, stage=False):
    out = Sample()
    if stage:
        ok = lib.ws_river_stage(C.byref(r), C.byref(s), 0, t, C.byref(out))
    else:
        ok = lib.ws_river_sample(C.byref(r), 0, t, C.byref(out))
    assert ok, "sample failed"
    return {
        "x": out.x, "y": out.y, "z": out.z,
        "tangent_x": out.tangent_x, "tangent_z": out.tangent_z,
        "width": out.width, "depth": out.depth,
        "flow": out.flow, "surfaced": out.surfaced,
    }


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
    results = {"stage": "resource-ecology", "checks": 0}

    def check(cond, label):
        results["checks"] += 1
        assert cond, label

    check(C.sizeof(State) == 10920, "State ctypes layout drift")

    # 1. Committed artifacts are exactly the generator output (schema 4).
    built = macro.build(SEED)
    src, data, manifest = built["src"], built["data"], built["manifest"]
    check(manifest["schema"] == 4, "committed product is not schema 4")
    check(src == json.loads((ROOT / "content/worlds/macro.json").read_text()), "committed macro.json drift")
    check(data == (ROOT / "build/macro.cws").read_bytes(), "committed product bytes drift")
    inc = (ROOT / "content/worlds/macro.inc").read_text()
    inc_bytes = bytes(int(v) for v in re.findall(r"\d+", inc.split("{", 1)[1]))
    check(inc_bytes == data, "committed macro.inc drift")
    r = Recipe()
    check(lib.ws_load(C.byref(r), data, len(data)) == 0, "C rejects the macro product")
    check(r.reservoirs_n == 4, "reservoir count")
    for i, v in enumerate(src["reservoirs"]):
        rec = r.reservoirs[i]
        check(rec.kind == RESERVOIR_KINDS[v["kind"]], f"reservoir {i} kind")
        check((rec.zone, rec.capacity, rec.level, rec.rate) == (v["zone"], v["capacity"], v["level"], v["rate"]), f"reservoir {i} parameters")
        check((rec.lo.x, rec.lo.y, rec.lo.z) == tuple(v["lo"]) and (rec.hi.x, rec.hi.y, rec.hi.z) == tuple(v["hi"]), f"reservoir {i} region")
    _, _, frecs, recs = runtime_view(src)
    frec = frecs[0]

    # 2. Weather parity: C and Python agree on the climate schedule.
    for d in range(100):
        check(lib.ws_weather(C.byref(r), d) == weather(src["seed"], d), f"weather parity day {d}")

    # 3. Stage parity across the lake reach at many drawdown levels.
    s = State()
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    for lvl in (0, 37860, 72000, 103395, 150000, 168930, 234465, 300000):
        s.level[R_LAKE] = lvl
        m["level"][R_LAKE] = lvl
        for t in list(range(30000, 42001, 256)) + [36000, 36400, 31456]:
            c = c_sample(lib, r, s, t, stage=True)
            p = river_stage(frec, recs, m, t)
            check(c == p, f"stage parity lvl {lvl} t {t}")
        check(m["level"][R_LAKE] == lvl, "mirror untouched by stage")
    # Exact committed numbers (independent of both implementations).
    s.level[R_LAKE] = 234465
    check(c_sample(lib, r, s, 36000, stage=True)["width"] == 37513, "drawdown width literal")
    s.level[R_LAKE] = 72000
    g = c_sample(lib, r, s, 36000, stage=True)
    check((g["width"], g["depth"], g["surfaced"]) == (11519, 647, 0), "marsh literals")
    g = c_sample(lib, r, s, 31456, stage=True)
    check((g["width"], g["depth"], g["surfaced"]) == (2879, 215, 1), "plain reach keeps its strip")
    s.level[R_LAKE] = 300000

    def center(i):
        v = r.reservoirs[i]
        return ((v.lo.x + v.hi.x) // 2, (v.lo.y + v.hi.y) // 2, (v.lo.z + v.hi.z) // 2)

    def apply_op(action, target, seq, amount, aux, at, server=0):
        op = Operation(seq, 1, s.revision, action, target, amount, aux)
        ctx = Context(0x5151, Address(Pos(*at), 0xFFFF), server)
        return lib.ws_apply(C.byref(s), C.byref(r), ctx, op)

    def sync(label):
        n = s.reservoir_count
        check(list(s.level[:n]) == m["level"][:n], f"{label}: levels")
        check(s.clock_s == m["clock_s"], f"{label}: clock")
        check(list(s.material[: s.player_count]) == m["material"][: s.player_count], f"{label}: material")
        check(list(s.shards[: s.player_count]) == m["shards"][: s.player_count], f"{label}: shards")
        check((s.recovered_total, s.used_total, s.lost_total) == (m["recovered"], m["used"], m["lost"]), f"{label}: ledger totals")
        check(s.site_count == len(m["sites"]), f"{label}: site count")
        for i in range(s.site_count):
            c, p = s.sites[i], m["sites"][i]
            check((c.reservoir, c.kind, c.extent, c.amount, c.pos.x, c.pos.y, c.pos.z) == (p["reservoir"], p["kind"], p["extent"], p["amount"], *p["pos"]), f"{label}: site {i}")
        c_ok = lib.ws_ledger_check(C.byref(s), C.byref(r))
        check(c_ok == 1 and ledger_check(m, recs), f"{label}: ledger identity")
        check(lib.ws_state_validate(C.byref(s), C.byref(r)) == 0, f"{label}: state valid")

    def tick(delta):
        lib.ws_resources_tick(C.byref(s), C.byref(r), delta)
        resources_tick(m, recs, src["seed"], delta)
        sync("tick")

    # 4. Case 1: excavation, conversion, time advance.
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    at = center(R_SPOIL)
    check(apply_op(EXCAVATE, R_SPOIL, 1, 30000, 0, at).status == 0, "pit excavation")
    m["level"][R_SPOIL] -= 30000
    m["material"][0] += 30000
    m["sites"].append({"pos": at, "reservoir": R_SPOIL, "kind": PIT, "extent": 30000, "amount": 30000})
    sync("pit")
    cave_mouth = (-223315, 90300, -565000)
    check(apply_op(EXCAVATE, R_SPOIL, 2, 5000, 1, cave_mouth).status == 0, "foundation excavation")
    m["level"][R_SPOIL] -= 5000
    m["material"][0] += 5000
    m["sites"].append({"pos": cave_mouth, "reservoir": R_SPOIL, "kind": SITE, "extent": 5000, "amount": 5000})
    sync("foundation")
    check(apply_op(CONVERT, R_SPOIL, 3, 30000, 0, at).status == 0, "refine 3:1")
    m["material"][0] -= 30000
    m["shards"][0] += 10000
    m["lost"] += 20000
    sync("refine")
    check(apply_op(SPEND, R_SPOIL, 4, 4000, 0, at).status == 0, "spend shards")
    m["shards"][0] -= 4000
    m["used"] += 4000
    sync("spend")
    for _ in range(20):
        tick(86400)
    check(s.level[R_SPOIL] == 400000 and s.recovered_total == 65000, "terrain recovered")
    check(s.lost_total == 50000, "buried + conversion loss")
    check(s.site_count == 1 and s.sites[0].kind == SITE and s.sites[0].amount == 5000, "excavation persists")

    # 5. Case 2: water overdraw, recovery, locality.
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    at = center(R_LAKE)
    for i in range(1, 6):
        d = apply_op(EXCAVATE, R_LAKE, i, 65535, 0, at)
        check(d.status == 0, f"water draw {i}")
        take = min(65535, m["level"][R_LAKE])
        m["level"][R_LAKE] -= take
        m["used"] += take
        sync(f"draw {i}")
    check(s.level[R_LAKE] == 0 and s.used_total == 300000 and s.site_count == 0, "depleted")
    check(apply_op(EXCAVATE, R_LAKE, 6, 100, 0, at).status == DENIED, "empty reservoir denies")
    check(apply_op(EXCAVATE, R_LAKE, 6, 1000, 0, center(R_SPOIL)).status == DENIED, "client outside region denied")
    check(s.used_total == 300000, "denied ops are transactional")
    for day in range(10):
        tick(86400)
    after = [48000, 72000, 120000, 192000, 216000, 264000, 288000, 300000, 300000, 300000]
    check(s.level[R_LAKE] == after[-1] and s.recovered_total == 300000, "rainfall recovery")
    check(c_sample(lib, r, s, 36000, stage=True)["surfaced"] == 1, "strip restored")
    check(apply_op(EXCAVATE, R_LAKE, 7, 1000, 0, center(R_SPOIL), server=1).status == 0, "server draws anywhere")
    m["level"][R_LAKE] -= 1000
    m["used"] += 1000
    sync("server draw")

    # 6. Case 3: Phos depletion and recharge with pit healing.
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    at = center(R_KARST)
    check(apply_op(EXCAVATE, R_KARST, 1, 50000, 0, at).status == 0, "phos excavation")
    m["level"][R_KARST] -= 50000
    m["shards"][0] += 50000
    m["sites"].append({"pos": at, "reservoir": R_KARST, "kind": PIT, "extent": 50000, "amount": 50000})
    sync("phos pit")
    for _ in range(6):
        tick(86400)
    check(s.level[R_KARST] == 160000 and s.recovered_total == 100000, "phos recharged")
    check(s.lost_total == 50000 and s.site_count == 0, "leached pit healed")

    # 7. Case 4: biomass harvest leaves no scar.
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    at = center(R_FOREST)
    check(apply_op(EXCAVATE, R_FOREST, 1, 20000, 0, at).status == 0, "biomass harvest")
    m["level"][R_FOREST] -= 20000
    m["material"][0] += 20000
    sync("harvest")
    check(s.site_count == 0, "no scar")
    for _ in range(5):
        tick(86400)
    check(s.level[R_FOREST] == 200000 and s.recovered_total == 20000, "biomass regrew")

    # 8. Case 5: wire round trip, rejections, lossy round trip.
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    at = center(R_SPOIL)
    check(apply_op(EXCAVATE, R_SPOIL, 1, 30000, 0, at).status == 0, "wire pit")
    check(apply_op(EXCAVATE, R_SPOIL, 2, 5000, 1, cave_mouth).status == 0, "wire foundation")
    check(apply_op(CONVERT, R_SPOIL, 3, 30000, 0, at).status == 0, "wire refine")
    check(apply_op(SPEND, R_SPOIL, 4, 4000, 0, at).status == 0, "wire spend")
    buf = (C.c_ubyte * 12000)()
    n = lib.ws_state_encode(C.byref(s), buf, 12000)
    base = 48 + 22 * 28 + 538 + 12 + 4 * 16 + 4 * 20
    check(n == base + 8 + 16 + 4 + 2 * 24 + 24 and buf[4] == 2, "wire v2 layout")
    t = State()
    check(lib.ws_state_decode(C.byref(t), C.byref(r), buf, n) == 0, "decode")
    check(lib.ws_state_hash(C.byref(t)) == lib.ws_state_hash(C.byref(s)), "hash identity")
    check(t.site_count == 2 and t.sites[1].amount == 5000, "sites carried")
    check(lib.ws_state_decode(C.byref(t), C.byref(r), buf, n - 1) != 0, "truncation rejected")
    check(lib.ws_save(C.byref(s), b"build/resource-state-py") == 1, "save")
    u = State()
    check(lib.ws_restore(C.byref(u), C.byref(r), b"build/resource-state-py") == 1, "restore")
    check(lib.ws_state_hash(C.byref(u)) == lib.ws_state_hash(C.byref(s)), "save/restore identity")
    # merge rejects resource ops (offline resource changes are not mergeable)
    op = Operation(9, 1, s.revision, EXCAVATE, R_SPOIL, 1000, 0)
    ctx = Context(0x5151, Address(Pos(*at), 0xFFFF), 0)
    base_s = State()
    lib.ws_state_init(C.byref(base_s), C.byref(r))
    lib.ws_join(C.byref(base_s), 0x5151)
    check(lib.ws_merge(C.byref(s), C.byref(base_s), C.byref(r), ctx, op).status == DENIED, "merge rejects resource ops")
    # fail-closed ops leave the state untouched
    before = (s.material[0], s.shards[0], s.level[R_SPOIL], s.site_count, s.revision)
    for args in (
        (EXCAVATE, 4, 9, 1000, 0, at, 0),
        (EXCAVATE, R_SPOIL, 9, 0, 0, at, 0),
        (CONVERT, R_SPOIL, 10, 30000, 2, at, 0),
        (CONVERT, R_SPOIL, 10, 2, 0, at, 0),
        (CONVERT, R_SPOIL, 10, 30000, 0, at, 0),
        (SPEND, R_SPOIL, 10, 6001, 0, at, 0),
        (EXCAVATE, R_SPOIL, 9, 1000, 0, center(R_LAKE), 0),
    ):
        action, target, seq, amount, aux, pos, srv = args
        check(apply_op(action, target, seq, amount, aux, pos, server=srv).status != 0, f"rejected {action}/{amount}")
    op = Operation(9, 2, s.revision, EXCAVATE, R_SPOIL, 1000, 0)
    ctx = Context(0x5151, Address(Pos(*at), 0xFFFF), 0)
    check(lib.ws_apply(C.byref(s), C.byref(r), ctx, op).status == DENIED, "wrong epoch rejected")
    op.epoch, op.sequence, op.base_revision = 1, 4, s.revision
    check(lib.ws_apply(C.byref(s), C.byref(r), ctx, op).status == STALE, "replay is stale")
    op.sequence, op.base_revision = 9, s.revision + 1
    check(lib.ws_apply(C.byref(s), C.byref(r), ctx, op).status == STALE, "stale base revision")
    check((s.material[0], s.shards[0], s.level[R_SPOIL], s.site_count) == before[:4], "rejections transactional")
    # carry overflow is WS_FULL (9), transactional
    check(apply_op(EXCAVATE, R_SPOIL, 11, 60000, 0, at).status == 0, "fill carry")
    check(s.material[0] == 65000, "carry at cap")
    lvl, sites = s.level[R_SPOIL], s.site_count
    check(apply_op(EXCAVATE, R_SPOIL, 12, 1000, 0, at).status == 9, "carry overflow is FULL")
    check(s.level[R_SPOIL] == lvl and s.site_count == sites, "overflow transactional")
    # bounded lossy round trip: 6 material -> 2 shards -> 1 material
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 0x5151)
    m = mirror_state(recs)
    check(apply_op(EXCAVATE, R_SPOIL, 1, 6, 0, at).status == 0, "tiny dig")
    m["level"][R_SPOIL] -= 6
    m["material"][0] += 6
    m["sites"].append({"pos": at, "reservoir": R_SPOIL, "kind": PIT, "extent": 6, "amount": 6})
    sync("tiny dig")
    check(apply_op(CONVERT, R_SPOIL, 2, 6, 0, at).status == 0, "6 -> 2 shards")
    m["material"][0] -= 6
    m["shards"][0] += 2
    m["lost"] += 4
    sync("6 -> 2")
    check(apply_op(CONVERT, R_SPOIL, 3, 2, 1, at).status == 0, "2 shards -> 1 material")
    m["shards"][0] -= 2
    m["material"][0] += 1
    m["lost"] += 1
    sync("2 -> 1")
    check((s.material[0], s.shards[0], s.lost_total) == (1, 0, 5), "lossy round trip")
    check(s.tail[0].action == EXCAVATE and s.tail[2].action == CONVERT, "tail history")
    check(s.feed[0].kind == DISCOVERY and s.feed[0].target == R_SPOIL, "feed history")

    # 9. The compiler rejects invalid reservoir CONTENT fail-closed.
    def reservoir_source():
        return {
            "schema": 1, "generator": 1, "ancestry": [9, 9, 9, 9], "seed": 77,
            "epoch": 1, "revision": 0, "name": "probe",
            "modules": [{"name": "ground", "key": 1, "level": "region", "shape": "plane", "tags": [], "position": [0, 0, 0], "size": [1000, 100, 1000]}],
            "links": [],
            "reservoirs": [
                {"name": "a", "key": 201, "kind": "water", "zone": 1, "lo": [0, 0, 0], "hi": [100000, 10000, 100000], "capacity": 1000, "level": 1000, "rate": 10},
                {"name": "b", "key": 202, "kind": "phos", "zone": 0, "lo": [200000, 0, 0], "hi": [300000, 10000, 100000], "capacity": 1000, "level": 1000, "rate": 10},
            ],
        }

    def rejects(mutation, label):
        st = reservoir_source()
        mutation(st)
        try:
            compile_recipe(st)
        except ValueError:
            check(True, label)
        else:
            raise AssertionError(f"compiler accepted {label}")

    rejects(lambda st: st["reservoirs"][0].__setitem__("kind", "aether"), "unknown kind")
    rejects(lambda st: st["reservoirs"][0].__setitem__("zone", 4), "zone out of range")
    rejects(lambda st: st["reservoirs"][0]["lo"].__setitem__(0, 100000), "degenerate region")
    rejects(lambda st: st["reservoirs"][0].__setitem__("level", 1001), "level beyond capacity")
    rejects(lambda st: st["reservoirs"][0].__setitem__("rate", 0), "zero rate")
    rejects(lambda st: st["reservoirs"][0].__setitem__("rate", 65536), "rate overflow")
    rejects(lambda st: st["reservoirs"][0].__setitem__("capacity", 400001), "capacity overflow")
    rejects(lambda st: st["reservoirs"][0].__setitem__("mass", 1), "unknown field")
    rejects(lambda st: st["reservoirs"][1].__setitem__("name", "a"), "duplicate name")
    rejects(lambda st: st["reservoirs"].append({**st["reservoirs"][0], "name": "c", "key": 203}), "same-kind overlap")
    rejects(
        lambda st: st["reservoirs"].extend(
            {**st["reservoirs"][0], "name": f"n{i}", "key": 300 + i, "lo": [400000 + i * 80000, 0, 0], "hi": [470000 + i * 80000, 10000, 100000]}
            for i in range(7)
        ),
        "reservoir capacity exceeded",
    )

    # 10. Coupled source: biomass recovery scales with the paired Phos lode.
    def coupled_source(phos_full=True):
        st = reservoir_source()
        st["seed"] = 4242
        st["reservoirs"] = [
            {"name": "wood", "key": 201, "kind": "biomass", "zone": 0, "lo": [0, 0, 0], "hi": [200000, 10000, 200000], "capacity": 100000, "level": 100000, "rate": 10000},
            {"name": "lode", "key": 202, "kind": "phos", "zone": 0, "lo": [50000, -10000, 50000], "hi": [150000, 0, 150000], "capacity": 50000, "level": 50000 if phos_full else 10000, "rate": 1000},
        ]
        return st

    def coupled_run(phos_full):
        st = coupled_source(phos_full)
        pdata, _ = compile_recipe(st)
        pr = Recipe()
        check(lib.ws_load(C.byref(pr), pdata, len(pdata)) == 0, "coupled product loads")
        ps = State()
        lib.ws_state_init(C.byref(ps), C.byref(pr))
        lib.ws_join(C.byref(ps), 0x5151)
        _, _, _, precs = runtime_view(st)
        pm = mirror_state(precs)
        v = pr.reservoirs[0]
        at = ((v.lo.x + v.hi.x) // 2, (v.lo.y + v.hi.y) // 2, (v.lo.z + v.hi.z) // 2)
        seq = 1
        if not phos_full:
            # drain the lode to a fifth through the C op path
            w = pr.reservoirs[1]
            wat = ((w.lo.x + w.hi.x) // 2, (w.lo.y + w.hi.y) // 2, (w.lo.z + w.hi.z) // 2)
            op = Operation(seq, 1, ps.revision, EXCAVATE, 1, 40000, 0)
            ctx = Context(0x5151, Address(Pos(*wat), 0xFFFF), 1)
            check(lib.ws_apply(C.byref(ps), C.byref(pr), ctx, op).status == OK, "drain lode")
            take = min(40000, pm["level"][1])
            pm["level"][1] -= take
            pm["shards"][0] += take
            pm["sites"].append({"pos": wat, "reservoir": 1, "kind": PIT, "extent": take, "amount": take})
            seq += 1
        # harvest 40000 biomass so there is room to regrow
        op = Operation(seq, 1, ps.revision, EXCAVATE, 0, 40000, 0)
        ctx = Context(0x5151, Address(Pos(*at), 0xFFFF), 1)
        check(lib.ws_apply(C.byref(ps), C.byref(pr), ctx, op).status == OK, "harvest wood")
        pm["level"][0] -= 40000
        pm["material"][0] += 40000
        lib.ws_resources_tick(C.byref(ps), C.byref(pr), 86400)
        resources_tick(pm, precs, st["seed"], 86400)
        check(list(ps.level[:2]) == pm["level"][:2], "coupled tick parity")
        check((ps.recovered_total, ps.used_total, ps.lost_total) == (pm["recovered"], pm["used"], pm["lost"]), "coupled totals parity")
        check(ps.site_count == len(pm["sites"]), "coupled sites parity")
        check(lib.ws_ledger_check(C.byref(ps), C.byref(pr)) == 1 and ledger_check(pm, precs), "coupled ledger")
        return ps.level[0]

    full = coupled_run(True)
    drained = coupled_run(False)
    check(full > drained, "phos stock scales biomass recovery")
    results["coupled_biomass_level"] = {"phos_full": full, "phos_drained": drained}

    # 11. Generalization: more seeds run the same proof skeleton.
    results["generalized_seeds"] = []
    for seed in (1, 3735928559, 20260922):
        gen = macro.build(seed)
        gsrc, gdata = gen["src"], gen["data"]
        check(lib.ws_load(C.byref(r), gdata, len(gdata)) == 0, f"seed {seed:#x} product rejected")
        _, _, grecs_f, grecs = runtime_view(gsrc)
        gfrec = grecs_f[0]
        lib.ws_state_init(C.byref(s), C.byref(r))
        lib.ws_join(C.byref(s), 0x5151)
        m = mirror_state(grecs)
        v = r.reservoirs[R_LAKE]
        at = ((v.lo.x + v.hi.x) // 2, (v.lo.y + v.hi.y) // 2, (v.lo.z + v.hi.z) // 2)
        check(apply_op(EXCAVATE, R_LAKE, 1, 65535, 0, at).status == 0, f"seed {seed:#x} draw")
        take = min(65535, m["level"][R_LAKE])
        m["level"][R_LAKE] -= take
        m["used"] += take
        for _ in range(3):
            lib.ws_resources_tick(C.byref(s), C.byref(r), 86400)
            resources_tick(m, grecs, gsrc["seed"], 86400)
        check(list(s.level[:4]) == m["level"][:4], f"seed {seed:#x} tick parity")
        check(lib.ws_ledger_check(C.byref(s), C.byref(r)) == 1 and ledger_check(m, grecs), f"seed {seed:#x} ledger")
        for t in (36000, 36400, 31456):
            c = c_sample(lib, r, s, t, stage=True)
            p = river_stage(gfrec, grecs, m, t)
            check(c == p, f"seed {seed:#x} stage parity t {t}")
        results["generalized_seeds"].append({"seed": seed, "level": list(s.level[:4])})

    (ROOT / "results/worldsdk").mkdir(parents=True, exist_ok=True)
    (ROOT / "results/worldsdk/resource-test.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
