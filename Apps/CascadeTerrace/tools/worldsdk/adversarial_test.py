#!/usr/bin/env python3
"""Adversarial spatial suite (mission §48). Generated recipes, not authored
worlds: the same SDK must express flat streets, steep hillslides, terraces,
multi-elevation towers, mismatched floors, rooftops, stairs, lifts, caves,
tunnels, secrets, jump-required optionals, latent apartments and vendors —
and must fail generation cleanly on impossible requested connections.
Traversal uses the probe method: 100 mm swept steps, runtime ground and
collision, never a teleport fallback."""

import ctypes as C, json, math
from qualify import ROOT, Recipe, Module, Pos
from server import Address, Traveler, library
from sdk import compile_recipe

REACH = {"WALK": 0, "ABILITY": 1, "CONDITIONAL": 2, "INACCESSIBLE": 3, "INVALID": 4}
CAP_WALK, CAP_LIFT, CAP_PORTAL = 1, 2, 4
WS_DISCONNECTED = 6
WS_BOUNDS = 3


class Port(C.Structure):
    _fields_ = [("pos", Pos), ("owner", C.c_uint16), ("wall", C.c_uint16), ("mode", C.c_uint16), ("width", C.c_uint16)]


HIERARCHY = [
    {"name": "world", "key": 1, "parent": None, "level": "world", "shape": "box", "tags": [], "position": [0, 0, 0], "size": [1, 1, 1]},
    {"name": "region", "key": 2, "parent": "world", "level": "region", "shape": "box", "tags": [], "position": [0, 0, 0], "size": [1, 1, 1]},
    {"name": "settlement", "key": 3, "parent": "region", "level": "settlement", "shape": "box", "tags": [], "position": [0, 0, 0], "size": [1, 1, 1]},
    {"name": "district", "key": 4, "parent": "settlement", "level": "district", "shape": "box", "tags": [], "position": [0, 0, 0], "size": [1, 1, 1]},
]


def recipe(name, modules, links, seed=20260922):
    for i, m in enumerate(modules):
        m.setdefault("key", 100 + i)
        m.setdefault("parent", "district")
        m.setdefault("level", "structure")
        m.setdefault("tags", [])
    return {
        "schema": 1,
        "generator": 1,
        "ancestry": [191149311, 1, 2, 3],
        "seed": seed,
        "epoch": 1,
        "revision": 0,
        "name": name,
        "modules": HIERARCHY + modules,
        "links": links,
    }


def plane(name, position, size=(12000, 400, 12000), **kw):
    return {"name": name, "shape": "plane", "tags": ["Surface.Walk"], "position": list(position), "size": list(size), **kw}


def room(name, position, size=(12000, 6000, 12000), tags=("Surface.Walk", "Collision.Solid"), **kw):
    return {"name": name, "shape": "room", "tags": list(tags), "position": list(position), "size": list(size), **kw}


def harness(lib):
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_materialize.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Module)]
    lib.ws_move.argtypes = [C.POINTER(Recipe), C.POINTER(Address), C.c_int32, C.c_int32]
    lib.ws_use_link.argtypes = [C.POINTER(Recipe), C.POINTER(Traveler)]
    lib.ws_travel_tick.argtypes = [C.POINTER(Traveler), C.c_uint32]
    lib.ws_ground.argtypes = [C.POINTER(Recipe), Address, C.c_int32, C.POINTER(C.c_int32)]
    lib.ws_reachable.argtypes = [C.POINTER(Recipe), C.c_uint16, C.c_uint16, C.c_uint32]
    lib.ws_reachable.restype = C.c_int
    lib.ws_ports.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Port), C.c_int]
    lib.ws_validate.argtypes = [C.POINTER(Recipe)]
    lib.ws_validate.restype = C.c_int


def load(lib, src):
    data, manifest = compile_recipe(src)
    r = Recipe()
    assert lib.ws_load(C.byref(r), data, len(data)) == 0
    index = {m["name"]: m["index"] for m in manifest["modules"]}
    return r, index


def walk(lib, r, a, b):
    """Probe method: direct edge, 100 mm swept steps, runtime ground+collision."""
    A, B = Module(), Module()
    lib.ws_materialize(C.byref(r), a, C.byref(A))
    lib.ws_materialize(C.byref(r), b, C.byref(B))
    at = Address(Pos(A.pos.x, A.pos.y + 300, A.pos.z), 65535)
    for step in range(10000):
        dx, dz = B.pos.x - at.pos.x, B.pos.z - at.pos.z
        d = math.hypot(dx, dz)
        if d < 150:
            return True, step, [at.pos.x, at.pos.y, at.pos.z]
        if not lib.ws_move(C.byref(r), C.byref(at), int(dx / d * 100), int(dz / d * 100)):
            return False, step, [at.pos.x, at.pos.y, at.pos.z]
    return False, 10000, None


def walk_edges(lib, r, index):
    rows = []
    for l in r.links[: r.links_n]:
        if l.kind:
            continue
        ok, steps, stop = walk(lib, r, l.a, l.b)
        rows.append({"edge": [l.a, l.b], "pass": ok, "steps": steps, "stop": stop})
    return rows


def expect_reject(src, fragment):
    try:
        compile_recipe(src)
    except ValueError as e:
        assert fragment in str(e), f"expected {fragment!r} in rejection, got {str(e)!r}"
        return str(e)
    raise AssertionError(f"expected clean rejection containing {fragment!r}")


def reach(lib, r, a, b, caps):
    return lib.ws_reachable(C.byref(r), a, b, caps)


def use(lib, r, index):
    """Stand at a module's centre and take the first usable non-walk link."""
    M = Module()
    lib.ws_materialize(C.byref(r), index, C.byref(M))
    t = Traveler()
    t.at = Address(Pos(M.pos.x, M.pos.y, M.pos.z), 65535)
    assert lib.ws_use_link(C.byref(r), C.byref(t))
    return t


CASES = []


def case(fn):
    CASES.append(fn)
    return fn


@case
def flat_street_block(lib):
    src = recipe(
        "flat_street_block",
        [
            plane("street", [0, 2000, 0]),
            room("shop1", [-16000, 2000, -12000]),
            room("shop2", [0, 2000, -12000]),
            room("shop3", [16000, 2000, -12000]),
        ],
        [["street", "shop2", "walk"], ["shop1", "shop2", "walk"], ["shop2", "shop3", "walk"]],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 3, rows
    ports = (Port * 8)()
    assert lib.ws_ports(C.byref(r), index["shop2"], ports, 8) >= 3
    return {"edges": len(rows)}


@case
def steep_hillside(lib):
    src = recipe(
        "steep_hillside",
        [
            plane("t0", [0, 2000, 0]),
            plane("t1", [0, 10000, -20000]),
            plane("t2", [0, 18000, -40000]),
            plane("t3", [0, 26000, -60000]),
        ],
        [["t0", "t1", "walk"], ["t1", "t2", "walk"], ["t2", "t3", "walk"]],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 3, rows
    bad = recipe(
        "steep_hillside_impossible",
        [plane("t0", [0, 2000, 0]), plane("t1", [0, 10000, -6000])],
        [["t0", "t1", "walk"]],
    )
    expect_reject(bad, "slope exceeds baseline walk")
    return {"edges": len(rows)}


@case
def cascade_terrace(lib):
    src = json.loads((ROOT / "content/worlds/cascade.json").read_text())
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 6, rows
    t = use(lib, r, index["lift_base"])
    assert t.remaining > 0
    lib.ws_travel_tick(C.byref(t), t.remaining)
    top = Module()
    lib.ws_materialize(C.byref(r), index["lift_top"], C.byref(top))
    assert t.at.pos.y == top.pos.y and not t.remaining
    home = use(lib, r, index["home"])
    assert home.at.scope == index["owned_room"], home.at.scope
    return {"edges": len(rows), "lift_arrival_y": t.at.pos.y}


@case
def tower_multiple_street_elevations(lib):
    src = recipe(
        "tower_multiple_street_elevations",
        [
            plane("street_low", [0, 2000, 0]),
            room("tower", [-20000, 2000, 0], (12000, 14000, 12000)),
            plane("tower_top", [-20000, 16000, 0]),
            plane("street_high", [4000, 16000, 0]),
        ],
        [["street_low", "tower", "walk"], ["tower", "tower_top", "lift"], ["tower_top", "street_high", "walk"]],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 2, rows
    t = use(lib, r, index["tower"])
    lib.ws_travel_tick(C.byref(t), t.remaining)
    top = Module()
    lib.ws_materialize(C.byref(r), index["tower_top"], C.byref(top))
    assert t.at.pos.y == top.pos.y
    a, b = index["street_low"], index["street_high"]
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_LIFT) == REACH["ABILITY"]
    return {"edges": len(rows)}


@case
def bridge_mismatched_building_floors(lib):
    src = recipe(
        "bridge_mismatched_building_floors",
        [room("hall_a", [0, 5000, 0]), room("hall_b", [20000, 9000, 0])],
        [["hall_a", "hall_b", "walk"]],
    )
    r, index = load(lib, src)
    ok, _, _ = walk(lib, r, index["hall_a"], index["hall_b"])
    assert ok
    ok, _, _ = walk(lib, r, index["hall_b"], index["hall_a"])
    assert ok
    bad = recipe(
        "bridge_mismatched_impossible",
        [room("hall_a", [0, 5000, 0]), room("hall_b", [12000, 19000, 0])],
        [["hall_a", "hall_b", "walk"]],
    )
    expect_reject(bad, "slope exceeds baseline walk")
    return {"edges": 2}


@case
def rooftop_route(lib):
    src = recipe(
        "rooftop_route",
        [
            plane("street", [0, 2000, 14000], (40000, 400, 8000)),
            room("building_a", [0, 5000, 0], (12000, 8000, 12000)),
            plane("roof_a", [0, 13000, 0]),
            room("building_b", [24000, 5000, 0], (12000, 8000, 12000)),
            plane("roof_b", [24000, 13000, 0]),
        ],
        [
            ["street", "building_a", "walk"],
            ["building_a", "roof_a", "lift"],
            ["roof_a", "roof_b", "walk"],
            ["building_b", "roof_b", "lift"],
        ],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 2, rows
    a, b = index["street"], index["roof_b"]
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_LIFT) == REACH["ABILITY"]
    return {"edges": len(rows)}


@case
def stair_network(lib):
    src = recipe(
        "stair_network",
        [
            plane("landing", [0, 2000, 0]),
            {"name": "stair_n", "shape": "ramp", "tags": ["Surface.Walk"], "position": [0, 2000, 10000], "size": [4000, 800, 8000]},
            {"name": "stair_ne", "shape": "ramp", "tags": ["Surface.Walk"], "position": [16000, 2000, 10000], "size": [4000, 800, 8000]},
            plane("ledge_n", [0, 2800, 22000]),
            plane("ledge_ne", [16000, 2800, 22000]),
        ],
        [
            ["landing", "stair_n", "walk"],
            ["stair_n", "ledge_n", "walk"],
            ["landing", "stair_ne", "walk"],
            ["stair_ne", "ledge_ne", "walk"],
            ["ledge_n", "ledge_ne", "walk"],
        ],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 5, rows
    return {"edges": len(rows)}


@case
def lift(lib):
    src = recipe(
        "lift",
        [plane("base", [0, 3000, 0]), plane("top", [8000, 15000, 0])],
        [["base", "top", "lift"]],
    )
    r, index = load(lib, src)
    ok, steps, stop = walk(lib, r, index["base"], index["top"])
    assert not ok, "vertical rise must not be walkable; the lift is the route"
    t = use(lib, r, index["base"])
    assert t.remaining > 0
    lib.ws_travel_tick(C.byref(t), t.remaining)
    top = Module()
    lib.ws_materialize(C.byref(r), index["top"], C.byref(top))
    assert t.at.pos.y == top.pos.y and not t.remaining
    a, b = index["base"], index["top"]
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_LIFT) == REACH["ABILITY"]
    return {"walk_blocked_at_step": steps}


@case
def cave(lib):
    src = recipe(
        "cave",
        [plane("approach", [0, 3000, 20000]), room("cave", [0, 3000, 0], (16000, 8000, 16000))],
        [["approach", "cave", "walk"]],
    )
    r, index = load(lib, src)
    ok, _, stop = walk(lib, r, index["approach"], index["cave"])
    assert ok, stop
    ports = (Port * 8)()
    assert lib.ws_ports(C.byref(r), index["cave"], ports, 8) >= 2
    return {"stop": stop}


@case
def tunnel(lib):
    src = recipe(
        "tunnel",
        [
            plane("south_plaza", [0, 3000, -30000]),
            room("tunnel", [0, 3000, 0], (12000, 6500, 20000)),
            plane("north_plaza", [0, 3000, 30000]),
        ],
        [["south_plaza", "tunnel", "walk"], ["tunnel", "north_plaza", "walk"]],
    )
    r, index = load(lib, src)
    rows = walk_edges(lib, r, index)
    assert all(x["pass"] for x in rows) and len(rows) == 2, rows
    ports = (Port * 8)()
    assert lib.ws_ports(C.byref(r), index["tunnel"], ports, 8) >= 3
    return {"edges": len(rows)}


@case
def intentionally_inaccessible_secret(lib):
    src = recipe(
        "intentionally_inaccessible_secret",
        [
            plane("street", [0, 2000, 0]),
            room("secret", [30000, 2000, 0], (3000, 4000, 3000)),
        ],
        [["street", "secret", "portal"]],
    )
    r, index = load(lib, src)
    a, b = index["street"], index["secret"]
    ok, _, _ = walk(lib, r, a, b)
    assert not ok, "a secret behind a future ability is not baseline walkable"
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_PORTAL) == REACH["ABILITY"]
    t = use(lib, r, a)
    assert t.at.scope == 65535 and not t.remaining
    at = Address(Pos(t.at.pos.x, t.at.pos.y, t.at.pos.z), 65535)
    assert not lib.ws_move(C.byref(r), C.byref(at), 0, 1000), "sealed chamber walls must hold"
    return {"secret_scope": int(t.at.scope)}


@case
def jump_required_optional_path(lib):
    src = recipe(
        "jump_required_optional_path",
        [plane("ledge_a", [0, 10000, 0]), plane("ledge_b", [14000, 25000, 0])],
        [["ledge_a", "ledge_b", "lift"]],
    )
    r, index = load(lib, src)
    a, b = index["ledge_a"], index["ledge_b"]
    ok, _, _ = walk(lib, r, a, b)
    assert not ok, "the jump gap is not a walk route"
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_LIFT) == REACH["ABILITY"]
    bad = recipe(
        "jump_required_as_walk",
        [plane("ledge_a", [0, 10000, 0]), plane("ledge_b", [14000, 25000, 0])],
        [["ledge_a", "ledge_b", "walk"]],
    )
    expect_reject(bad, "slope exceeds baseline walk")
    return {}


@case
def latent_apartment(lib):
    src = recipe(
        "latent_apartment",
        [
            plane("street", [0, 2000, 20000]),
            room("home", [0, 5000, 0], (16000, 7000, 16000)),
            {"name": "apartment", "shape": "room", "level": "room", "tags": ["Surface.Walk", "Collision.Solid", "Scope.Interior"], "parent": "home", "position": [0, 5000, 0], "size": [10000, 4000, 10000]},
        ],
        [["street", "home", "walk"], ["home", "apartment", "portal"]],
    )
    r, index = load(lib, src)
    ok, _, _ = walk(lib, r, index["street"], index["home"])
    assert ok
    t = use(lib, r, index["home"])
    assert t.at.scope == index["apartment"] and not t.remaining
    y = C.c_int32()
    assert lib.ws_ground(C.byref(r), t.at, t.at.pos.y + 350, C.byref(y)) and y.value == t.at.pos.y
    ports = (Port * 8)()
    assert lib.ws_ports(C.byref(r), index["apartment"], ports, 8) >= 1
    a, b = index["street"], index["apartment"]
    assert reach(lib, r, a, b, CAP_WALK) == REACH["CONDITIONAL"]
    assert reach(lib, r, a, b, CAP_WALK | CAP_PORTAL) == REACH["ABILITY"]
    return {}


@case
def vendor_requires_baseline_public_access(lib):
    src = recipe(
        "vendor_public",
        [
            plane("street", [0, 2000, 0]),
            room("market_hall", [0, 5000, -16000]),
            {"name": "vault", "shape": "room", "level": "structure", "tags": ["Collision.Solid"], "position": [30000, 5000, 0], "size": [3000, 4000, 3000]},
            {"name": "vendor", "shape": "box", "level": "object", "tags": ["Entity.NPC"], "parent": "market_hall", "position": [0, 5000, -16000], "size": [600, 1700, 600]},
        ],
        [["street", "market_hall", "walk"]],
    )
    r, index = load(lib, src)
    ok, _, _ = walk(lib, r, index["street"], index["market_hall"])
    assert ok
    ports = (Port * 8)()
    assert lib.ws_ports(C.byref(r), index["market_hall"], ports, 8) >= 2
    # Runtime parity: re-parent the vendor into the sealed vault.
    vendor = index["vendor"]
    saved = r.modules[vendor].parent
    r.modules[vendor].parent = index["vault"]
    assert lib.ws_validate(C.byref(r)) == WS_DISCONNECTED
    r.modules[vendor].parent = saved
    assert lib.ws_validate(C.byref(r)) == 0
    # Compile-time parity: the same content is rejected before any product.
    bad = recipe(
        "vendor_sealed",
        [
            plane("street", [0, 2000, 0]),
            {"name": "vault", "shape": "room", "level": "structure", "tags": ["Collision.Solid"], "position": [30000, 5000, 0], "size": [3000, 4000, 3000]},
            {"name": "vendor", "shape": "box", "level": "object", "tags": ["Entity.NPC"], "parent": "vault", "position": [30000, 5000, 0], "size": [600, 1700, 600]},
        ],
        [],
    )
    expect_reject(bad, "Entity.NPC sealed inside a room with no public access port")
    return {}


@case
def impossible_requested_connection(lib):
    # Slope beyond baseline walk.
    bad_slope = recipe(
        "impossible_slope",
        [plane("a", [0, 2000, 0]), plane("b", [14000, 17000, 0])],
        [["a", "b", "walk"]],
    )
    expect_reject(bad_slope, "slope exceeds baseline walk")
    # Wall crossing that cannot host a 4000 mm port.
    bad_port = recipe(
        "impossible_port",
        [room("hall", [0, 5000, 0]), plane("plot", [20000, 2000, 16000])],
        [["hall", "plot", "walk"]],
    )
    expect_reject(bad_port, "cannot host a 4000 mm port")
    # Unrelated solid structure on the direct route.
    bad_blocked = recipe(
        "impossible_blocked",
        [
            room("hall_a", [0, 5000, 0]),
            room("hall_b", [40000, 5000, 0]),
            {"name": "slab", "shape": "box", "level": "object", "tags": ["Collision.Solid"], "position": [20000, 9000, 0], "size": [6000, 6000, 6000]},
        ],
        [["hall_a", "hall_b", "walk"]],
    )
    expect_reject(bad_blocked, "crosses unrelated solid structure")
    # Runtime parity: a validated product whose geometry is mutated apart.
    src = recipe(
        "impossible_runtime_base",
        [
            plane("street_low", [0, 2000, 0]),
            room("tower", [-20000, 2000, 0], (12000, 14000, 12000)),
            plane("tower_top", [-20000, 16000, 0]),
            plane("street_high", [4000, 16000, 0]),
        ],
        [["street_low", "tower", "walk"], ["tower", "tower_top", "lift"], ["tower_top", "street_high", "walk"]],
    )
    r, index = load(lib, src)
    saved = r.modules[index["street_high"]].pos.y
    r.modules[index["street_high"]].pos.y = 41000
    assert lib.ws_validate(C.byref(r)) == WS_BOUNDS
    r.modules[index["street_high"]].pos.y = saved
    assert lib.ws_validate(C.byref(r)) == 0
    return {"rejections": 3}


def main():
    lib = library()
    harness(lib)
    rows = []
    failures = []
    for fn in CASES:
        record = {"case": fn.__name__, "status": "PASS"}
        try:
            record.update(fn(lib) or {})
        except AssertionError as e:
            record["status"] = "FAIL"
            record["error"] = str(e)
            failures.append(record)
        rows.append(record)
        print(json.dumps(record))
    result = {
        "status": "PASS" if not failures else "FAILED",
        "passed": sum(1 for x in rows if x["status"] == "PASS"),
        "tested": len(rows),
        "gate": "Adversarial spatial cases; impossible connections must fail generation cleanly",
        "method": "Generated recipes compiled by the same SDK; traversal by 100 mm swept steps with runtime ground and collision.",
        "rows": rows,
    }
    (ROOT / "results/worldsdk/adversarial.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({k: v for k, v in result.items() if k != "rows"}))
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
