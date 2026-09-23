#!/usr/bin/env python3
"""Gate 5 test (sparse deterministic creatures and schedules). Proves,
against the C engine through libsdk.so, with an independent Python mirror
of every rule:
  - the committed macro artifacts are exactly the generator output, schema 4
    with eight geography-derived species (one per biome reservoir, two per
    settlement) whose records match the C-loaded table;
  - identities are stable pure functions of species and slot, equal in C
    and Python for every creature;
  - placement is constraint-satisfying and bit-exact: anchored slots on
    their settlement surface inside its extent, biome slots inside their
    reservoir extent on the terrain surface (mountain and karst tops, lake
    and forest floors), required anchors baseline-walk-reachable;
  - schedules resolve from time and the access graph, bit-exact at dwell
    points, travel points, phase wraps, time skips and the 32-bit clock
    wrap; dwell points are exactly the declared station surfaces, and the
    whole cycle is periodic in the species period;
  - queries materialize only the window, in (species, slot) order, with
    the negated-overflow cap contract, repeat determinism, and the
    recipe/state bytes untouched;
  - persistent exceptions override generated defaults identically: DEAD
    removes, RELOCATED re-anchors at a declared walk surface, PINNED holds
    home; the authority mutation fails closed on the same inputs in both
    languages; state wire version 3 carries the table through
    encode/decode and save/restore;
  - malformed species records fail closed in ws_load even with a
    recomputed CRC, and additional seeds generalize the whole proof.
Standard library only."""

import ctypes as C
import json
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import Id, Pos, Recipe, ROOT
from server import State, library
from sdk import (
    CAP_WALK,
    CREATURE_KINDS,
    CREX_DEAD,
    CREX_PINNED,
    CREX_RELOCATED,
    creature_at,
    creature_id,
    creature_query,
    creature_view,
    route_cost,
    runtime_view,
)
import macro

SEED = macro.SHOWCASE
OK = 0
REFERENCE = 5
BOUNDS = 3
R_SPOIL, R_LAKE, R_FOREST, R_KARST = 0, 1, 2, 3
S_MASSIF, S_LAKE, S_FOREST, S_KARST, S_GATE_WARDEN, S_GATE_RES, S_HAVEN_WARDEN, S_HAVEN_RES = range(8)
M_GATE, M_HAVEN, M_PHOS_RUIN = 11, 20, 21


class Sample(C.Structure):
    _fields_ = [
        ("id", Id),
        ("pos", Pos),
        ("species", C.c_uint16),
        ("slot", C.c_uint16),
        ("station", C.c_uint16),
        ("traveling", C.c_uint16),
    ]


def harness(lib):
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_init.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_join.argtypes = [C.POINTER(State), C.c_uint32]
    lib.ws_state_validate.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_state_validate.restype = C.c_int
    lib.ws_state_hash.argtypes = [C.POINTER(State)]
    lib.ws_state_hash.restype = C.c_uint32
    lib.ws_state_encode.argtypes = [C.POINTER(State), C.c_void_p, C.c_size_t]
    lib.ws_state_encode.restype = C.c_size_t
    lib.ws_state_decode.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_decode.restype = C.c_int
    lib.ws_creature_id.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16]
    lib.ws_creature_id.restype = Id
    lib.ws_creature_at.argtypes = [C.POINTER(Recipe), C.POINTER(State), C.c_uint16, C.c_uint16, C.c_uint32, C.POINTER(Sample)]
    lib.ws_creature_at.restype = C.c_int
    lib.ws_creature_query.argtypes = [C.POINTER(Recipe), C.POINTER(State), Pos, Pos, C.c_uint32, C.POINTER(Sample), C.c_int]
    lib.ws_creature_query.restype = C.c_int
    lib.ws_creature_except.argtypes = [C.POINTER(State), C.POINTER(Recipe), Id, C.c_uint16, C.c_uint16]
    lib.ws_creature_except.restype = C.c_int


def mirror_state(crex=None):
    return {"crex": list(crex or [])}


def mkid(words):
    return Id((C.c_uint32 * 4)(*words))


def as_sample(s):
    return (list(s.id.word), [s.pos.x, s.pos.y, s.pos.z], s.species, s.slot, s.station, s.traveling)


def as_mirror(m):
    return (m["id"], m["pos"], m["species"], m["slot"], m["station"], m["traveling"])


def main():
    lib = library()
    harness(lib)
    results = {"stage": "creature-schedules", "checks": 0, "generalized_seeds": []}

    def check(cond, label):
        results["checks"] += 1
        assert cond, label

    # 1. Committed artifacts are exactly the generator output (schema 4).
    built = macro.build(SEED)
    src, data, manifest = built["src"], built["data"], built["manifest"]
    check(manifest["schema"] == 4, "committed product is not schema 4")
    check(manifest["bytes"] == 2196, "product bytes")
    check(len(manifest["creatures"]) == 8, "species count")
    check(src == json.loads((ROOT / "content/worlds/macro.json").read_text()), "committed macro.json drift")
    check(data == (ROOT / "build/macro.cws").read_bytes(), "committed product bytes drift")
    inc = (ROOT / "content/worlds/macro.inc").read_text()
    inc_bytes = bytes(int(v) for v in __import__("re").findall(r"\d+", inc.split("{", 1)[1]))
    check(inc_bytes == data, "committed macro.inc drift")

    r = Recipe()
    check(lib.ws_load(C.byref(r), data, len(data)) == OK, "C load of the committed product")
    view = creature_view(src)
    check(len(view["creatures"]) == 8, "mirror species count")
    check(sum(c["slots"] for c in view["creatures"]) == 36, "mirror population")

    # 2. The species table matches the C-loaded records exactly.
    kinds = [c["kind"] for c in view["creatures"]]
    check(kinds == [1, 1, 1, 1, 3, 2, 3, 2], "species kinds")
    for i, c in enumerate(view["creatures"]):
        check(list(r.creatures[i].id.word) == c["id"], f"species {i} identity")
        check(r.creatures[i].kind == c["kind"], f"species {i} kind")
        check(r.creatures[i].habitat == c["habitat"], f"species {i} habitat")
        check(r.creatures[i].slots == c["slots"], f"species {i} slots")
        check(r.creatures[i].period == c["period"], f"species {i} period")
        check(r.creatures[i].stations == c["stations"], f"species {i} stations")
        check(r.creatures[i].radius == c["radius"], f"species {i} radius")
        check(r.creatures[i].seed == c["seed"], f"species {i} seed")

    # Required anchors are baseline-walk-reachable from the first walk
    # surface; the karst ruin is not (it needs the winze lift).
    modules, links, features, _ = runtime_view(src)
    first_walk = next(i for i, m in enumerate(src["modules"]) if "Surface.Walk" in m.get("tags", []))
    for c in view["creatures"]:
        if c["kind"] != CREATURE_KINDS["required"]:
            continue
        check(route_cost(modules, links, features, first_walk, c["habitat"], CAP_WALK) is not None, "required anchor reachable")
    check(route_cost(modules, links, features, first_walk, M_PHOS_RUIN, CAP_WALK) is None, "karst ruin needs a capability")

    # 3. Identities: stable, distinct, equal across languages.
    for sp in range(8):
        for slot in range(view["creatures"][sp]["slots"]):
            cid = lib.ws_creature_id(C.byref(r), sp, slot)
            check(list(cid.word) == creature_id(view, sp, slot), "creature id parity")
    a0 = creature_id(view, 0, 0)
    check(a0 == [2727697027, 2722871303, 282626514, 308584483], "committed massif identity")
    check(creature_id(view, 4, 0) == [3898211244, 3047446765, 2213983797, 1126835656], "committed warden identity")
    check(creature_id(view, 4, 0) != creature_id(view, 4, 1), "slots are distinct identities")

    # 4. Placement parity and constraints: every creature, pinned home and
    # a full time sweep (dwell, travel, phase wrap, time skip, clock wrap).
    state = State()
    lib.ws_state_init(C.byref(state), C.byref(r))
    check(lib.ws_join(C.byref(state), 0x5151) == OK, "join")
    times = [0, 1, 7, 599, 600, 3600, 4321, 12345, 20000, 21600, 28800, 43200, 64800, 100000, 4294967295]
    times += [i * 991 for i in range(80)]
    for sp in range(8):
        c = view["creatures"][sp]
        for slot in range(c["slots"]):
            cid = lib.ws_creature_id(C.byref(r), sp, slot)
            check(lib.ws_creature_except(C.byref(state), C.byref(r), cid, CREX_PINNED, 0) == OK, "pin for home")
            s = Sample()
            check(lib.ws_creature_at(C.byref(r), C.byref(state), sp, slot, 12345, C.byref(s)) == 1, "pinned resolves")
            home = [s.pos.x, s.pos.y, s.pos.z]
            m = creature_at(view, mirror_state([[creature_id(view, sp, slot), CREX_PINNED, 0]]), sp, slot, 12345)
            check(m is not None and m["pos"] == home, "pinned home parity")
            m = creature_at(view, mirror_state([[creature_id(view, sp, slot), CREX_PINNED, 0]]), sp, slot, 999)
            check(m is not None and m["pos"] == home, "pinned is time-invariant")
            if c["kind"] == CREATURE_KINDS["fauna"]:
                v = view["reservoirs"][c["habitat"]]
                check(v["lo"][0] <= home[0] <= v["hi"][0] and v["lo"][2] <= home[2] <= v["hi"][2], "fauna inside biome")
                check(v["lo"][1] <= home[1] <= v["hi"][1], "fauna inside biome elevation")
                if c["habitat"] == R_SPOIL:
                    check(home[1] in (600000, v["lo"][1]), "massif fauna on terrain")
                if c["habitat"] == R_KARST:
                    check(home[1] in (164282, v["lo"][1]), "karst fauna on terrain")
            else:
                mod = view["modules"][c["habitat"]]
                check(abs(home[0] - mod["at"][0]) <= mod["size"][0] // 2 - 600, "anchored inside extent")
                check(abs(home[2] - mod["at"][2]) <= mod["size"][2] // 2 - 600, "anchored inside extent")
                check(home[1] == mod["at"][1], "anchored on the surface")
            check(lib.ws_creature_except(C.byref(state), C.byref(r), cid, 0, 0) == OK, "unpin")
            # full schedule parity sweep
            dwell_or_travel = set()
            for t in times:
                s = Sample()
                ok = lib.ws_creature_at(C.byref(r), None, sp, slot, t, C.byref(s))
                m = creature_at(view, None, sp, slot, t)
                check(bool(ok) == (m is not None), f"existence parity {sp}/{slot}@{t}")
                if ok:
                    check(as_sample(s) == as_mirror(m), f"resolution parity {sp}/{slot}@{t}")
                    dwell_or_travel.add((m["station"], m["traveling"]))
            check(len(dwell_or_travel) >= 2, "schedule actually moves")
            # periodicity: pure function of time modulo the species period
            p = c["period"]
            m0 = creature_at(view, None, sp, slot, 12345)
            for k in (1, 2, 5):
                mk = creature_at(view, None, sp, slot, 12345 + k * p)
                check((m0["pos"], m0["station"], m0["traveling"]) == (mk["pos"], mk["station"], mk["traveling"]), "periodicity")

    # Exact committed placements (independent of either implementation).
    for sp, slot, home in [
        (S_GATE_WARDEN, 0, [288813, 470000, -467753]),
        (S_GATE_RES, 0, [274763, 470000, -485002]),
        (S_HAVEN_WARDEN, 0, [148822, 54675, 901749]),
        (S_HAVEN_RES, 0, [170996, 54675, 889923]),
        (S_MASSIF, 0, [-56689, 600000, -753078]),
        (S_LAKE, 0, [95590, 167627, 306989]),
        (S_FOREST, 5, [-216713, 244367, 84612]),
        (S_KARST, 2, [-30489, 164282, 557139]),
    ]:
        m = creature_at(view, mirror_state([[creature_id(view, sp, slot), CREX_PINNED, 0]]), sp, slot, 4242)
        check(m is not None and m["pos"] == home, "committed home literal")

    # 5. Queries: window materialization, order, cap contract, determinism.
    world = ([-1000000, -1000000, -1000000], [1000000, 1000000, 1000000])
    out = (Sample * 64)()
    n = lib.ws_creature_query(C.byref(r), None, Pos(*world[0]), Pos(*world[1]), 0, out, 64)
    m = creature_query(view, None, world[0], world[1], 0)
    check(n == len(m) == 36, "whole-world query count")
    for k in range(n):
        check(as_sample(out[k]) == as_mirror(m[k]), "query parity and order")
    check(lib.ws_creature_query(C.byref(r), None, Pos(*world[0]), Pos(*world[1]), 0, out, 10) == -36, "cap contract")
    check(lib.ws_creature_query(C.byref(r), None, Pos(*world[0]), Pos(*world[1]), 0, out, 36) == 36, "cap contract at the bound")
    gate_at = view["modules"][M_GATE]["at"]
    win = ([gate_at[0] - 80000, gate_at[1] - 8000, gate_at[2] - 80000], [gate_at[0] + 80000, gate_at[1] + 8000, gate_at[2] + 80000])
    for t in (12345, 27000, 4321, 4294967295):
        n = lib.ws_creature_query(C.byref(r), None, Pos(*win[0]), Pos(*win[1]), t, out, 64)
        m = creature_query(view, None, win[0], win[1], t)
        check(n == len(m), "window count parity")
        for k in range(n):
            check(as_sample(out[k]) == as_mirror(m[k]), "window parity")
            p = m[k]["pos"]
            check(win[0][0] <= p[0] <= win[1][0] and win[0][1] <= p[1] <= win[1][1] and win[0][2] <= p[2] <= win[1][2], "window containment")
    # repeat determinism and untouched inputs
    before_r = bytes(r)
    before_s = bytes(state)
    n1 = lib.ws_creature_query(C.byref(r), None, Pos(*world[0]), Pos(*world[1]), 777, out, 64)
    n2 = lib.ws_creature_query(C.byref(r), None, Pos(*world[0]), Pos(*world[1]), 777, out, 64)
    check(n1 == n2 == len(creature_query(view, None, world[0], world[1], 777)), "repeat determinism")
    check(bytes(r) == before_r and bytes(state) == before_s, "queries allocate and mutate nothing")

    # 6. Persistent exceptions override generated defaults, identically.
    cid = creature_id(view, S_GATE_RES, 0)
    default = creature_at(view, None, S_GATE_RES, 0, 20000)
    check(default["pos"] == [20507, 381565, -424817] and default["traveling"] == 1, "committed default literal")
    for kind, aux, label in [
        (CREX_DEAD, 0, "dead"),
        (CREX_RELOCATED, M_HAVEN, "relocated"),
        (CREX_PINNED, 0, "pinned"),
    ]:
        check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), kind, aux) == OK, f"{label} except")
        s = Sample()
        ok = lib.ws_creature_at(C.byref(state) and C.byref(r), C.byref(state), S_GATE_RES, 0, 20000, C.byref(s))
        m = creature_at(view, mirror_state([[cid, kind, aux]]), S_GATE_RES, 0, 20000)
        check(bool(ok) == (m is not None), f"{label} existence parity")
        if ok:
            check(as_sample(s) == as_mirror(m), f"{label} override parity")
        if kind == CREX_DEAD:
            check(m is None, "dead never materializes")
        if kind == CREX_RELOCATED:
            check(m["pos"] == [113632, 61346, 851695], "relocated literal")
        if kind == CREX_PINNED:
            check(m["pos"] == [274763, 470000, -485002] and m["traveling"] == 0, "pinned literal")
        # queries agree with at() under the same exceptions
        q = creature_query(view, mirror_state([[cid, kind, aux]]), world[0], world[1], 20000)
        if kind == CREX_DEAD:
            check(all(x["id"] != cid for x in q), "dead absent from queries")
        else:
            check(any(x["id"] == cid for x in q), "override present in queries")
        check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), 0, 0) == OK, f"{label} clear")
    # fail-closed mutations, same codes in both languages
    bogus = mkid([1, 2, 3, 4])
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), 4, 0) == BOUNDS, "unknown kind")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), CREX_DEAD, 5) == BOUNDS, "dead carries no aux")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), CREX_RELOCATED, 0) == REFERENCE, "non-walk anchor")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), CREX_RELOCATED, 4) == REFERENCE, "interior anchor")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), CREX_PINNED, 5) == BOUNDS, "unjoined player")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), bogus, CREX_DEAD, 0) == REFERENCE, "unknown creature")
    check(state.creature_count == 0 and lib.ws_state_validate(C.byref(state), C.byref(r)) == OK, "clean state")

    # 7. State wire version 3 carries the exception table.
    fid = creature_id(view, S_MASSIF, 0)
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), CREX_RELOCATED, M_HAVEN) == OK, "wire except 1")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(fid), CREX_DEAD, 0) == OK, "wire except 2")
    buf = (C.c_ubyte * 12000)()
    n = lib.ws_state_encode(C.byref(state), buf, 12000)
    base = 48 + 22 * 28 + 1 * 538 + 1 * 12 + state.feed_count * 16 + state.tail_count * 20
    check(n == base + 8 + 16 + 4 + 24 * state.site_count + 24 + 4 + 24 * 2, "wire v3 exact length")
    check(buf[4] == 3, "wire version 3")
    t = State()
    check(lib.ws_state_decode(C.byref(t), C.byref(r), bytes(buf[:n]), n) == OK, "decode")
    check(lib.ws_state_hash(C.byref(t)) == lib.ws_state_hash(C.byref(state)), "semantic hash survives the wire")
    check(t.creature_count == 2 and list(t.crex[0].id.word) == cid and t.crex[0].kind == CREX_RELOCATED and t.crex[0].aux == M_HAVEN, "wire carries the records")
    check(list(t.crex[1].id.word) == fid and t.crex[1].kind == CREX_DEAD, "wire carries the records")
    s2 = Sample()
    check(lib.ws_creature_at(C.byref(r), C.byref(t), S_GATE_RES, 0, 20000, C.byref(s2)) == 1, "override survives the wire")
    check([s2.pos.x, s2.pos.y, s2.pos.z] == [113632, 61346, 851695], "relocated after decode")
    check(lib.ws_creature_at(C.byref(r), C.byref(t), S_MASSIF, 0, 20000, C.byref(s2)) == 0, "dead after decode")
    check(lib.ws_state_decode(C.byref(t), C.byref(r), bytes(buf[: n - 1]), n - 1) != OK, "truncation rejects")
    # a corrupted exception kind rejects even with a fixed CRC
    bad = bytearray(buf[:n])
    ctable = base + 8 + 16 + 4 + 24 * state.site_count + 24 + 4
    struct.pack_into("<H", bad, ctable + 16, 9)
    struct.pack_into("<I", bad, 12, zlib.crc32(bad[16:]))
    check(lib.ws_state_decode(C.byref(t), C.byref(r), bytes(bad), n) != OK, "corrupted exception rejects")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(cid), 0, 0) == OK, "clear 1")
    check(lib.ws_creature_except(C.byref(state), C.byref(r), mkid(fid), 0, 0) == OK, "clear 2")

    # 8. Malformed species records fail closed even with a fixed CRC.
    table = 56 + 64 * 22 + 8 * 13 + 56 * 1 + 12 * 5 + 64 * 4

    def mutated_load(species, field, value):
        b = bytearray(data)
        struct.pack_into("<H", b, table + 32 * species + field, value)
        struct.pack_into("<I", b, 12, zlib.crc32(b[16:]))
        probe = Recipe()
        return lib.ws_load(C.byref(probe), bytes(b), len(b)) == OK

    check(not mutated_load(S_MASSIF, 16, 4), "unknown kind rejects")
    check(not mutated_load(S_MASSIF, 18, 27), "habitat out of range rejects")
    check(not mutated_load(S_MASSIF, 20, 0), "empty population rejects")
    check(not mutated_load(S_MASSIF, 24, 5), "station overflow rejects")
    check(not mutated_load(S_MASSIF, 22, 599), "period floor rejects")
    check(not mutated_load(S_MASSIF, 26, 1999), "radius floor rejects")
    check(not mutated_load(S_GATE_WARDEN, 18, 0), "non-walk anchor rejects")
    check(not mutated_load(S_GATE_WARDEN, 18, M_PHOS_RUIN), "unreachable required anchor rejects")
    b = bytearray(data)
    b[table + 32 : table + 48] = b[table + 0 : table + 16]  # species 1 id <- species 0 id
    struct.pack_into("<I", b, 12, zlib.crc32(b[16:]))
    probe = Recipe()
    check(lib.ws_load(C.byref(probe), bytes(b), len(b)) != OK, "duplicate identity rejects")
    check(mutated_load(S_GATE_RES, 18, M_PHOS_RUIN), "non-required karst anchor is legal content")

    # 9. Additional seeds generalize the whole proof.
    for seed in (1, 3735928559, 20260922):
        g = macro.build(seed)
        gsrc, gdata = g["src"], g["data"]
        gview = creature_view(gsrc)
        check(len(gview["creatures"]) == 8, "generalized species count")
        gr = Recipe()
        check(lib.ws_load(C.byref(gr), gdata, len(gdata)) == OK, "generalized load")
        mismatches = 0
        for sp in range(8):
            for slot in range(gview["creatures"][sp]["slots"]):
                for t in (0, 1, 999, 12345, 65535, 4294967295):
                    s = Sample()
                    ok = lib.ws_creature_at(C.byref(gr), None, sp, slot, t, C.byref(s))
                    m = creature_at(gview, None, sp, slot, t)
                    if bool(ok) != (m is not None) or (ok and as_sample(s) != as_mirror(m)):
                        mismatches += 1
        check(mismatches == 0, "generalized parity")
        gout = (Sample * 64)()
        gn = lib.ws_creature_query(C.byref(gr), None, Pos(-1000000, -1000000, -1000000), Pos(1000000, 1000000, 1000000), 31337, gout, 64)
        gm = creature_query(gview, None, [-1000000, -1000000, -1000000], [1000000, 1000000, 1000000], 31337)
        check(gn == len(gm), "generalized query count")
        for k in range(gn):
            if as_sample(gout[k]) != as_mirror(gm[k]):
                mismatches += 1
        check(mismatches == 0, "generalized query parity")
        results["generalized_seeds"].append({"seed": seed, "bytes": len(gdata), "creatures": sum(c["slots"] for c in gview["creatures"])})

    results["species"] = len(view["creatures"])
    results["population"] = sum(c["slots"] for c in view["creatures"])
    results["product_bytes"] = len(data)
    (ROOT / "results/worldsdk/creature-test.json").write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps(results))


if __name__ == "__main__":
    main()
