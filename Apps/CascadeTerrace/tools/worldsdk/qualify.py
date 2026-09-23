#!/usr/bin/env python3
"""Determinism/parity/adversarial product qualification, reproducible seed 20260922."""

import ctypes as C, copy, hashlib, json, pathlib, random, struct, time, zlib
from sdk import compile_recipe, h

ROOT = pathlib.Path(__file__).resolve().parents[2]


class Id(C.Structure):
    _fields_ = [("word", C.c_uint32 * 4)]


class Pos(C.Structure):
    _fields_ = [("x", C.c_int32), ("y", C.c_int32), ("z", C.c_int32)]


class Module(C.Structure):
    _fields_ = [
        ("id", Id),
        ("parent", C.c_uint16),
        ("kind", C.c_uint16),
        ("flags", C.c_uint16),
        ("phos", C.c_uint16),
        ("pos", Pos),
        ("size", Pos),
        ("color", C.c_uint32),
        ("seed", C.c_uint32),
        ("jx", C.c_int16),
        ("jz", C.c_int16),
        ("affinity", C.c_uint16),
        ("quantity", C.c_uint16),
    ]


class Link(C.Structure):
    _fields_ = [
        ("a", C.c_uint16),
        ("b", C.c_uint16),
        ("kind", C.c_uint16),
        ("reserved", C.c_uint16),
    ]


class Feature(C.Structure):
    _fields_ = [
        ("id", Id),
        ("kind", C.c_uint16),
        ("flow", C.c_uint16),
        ("up", Pos),
        ("down", Pos),
        ("width", C.c_uint16),
        ("depth", C.c_uint16),
        ("seed", C.c_uint32),
        ("reserved", C.c_uint32),
    ]


class Exc(C.Structure):
    _fields_ = [
        ("feature", C.c_uint16),
        ("type", C.c_uint16),
        ("at", C.c_uint16),
        ("length", C.c_uint16),
        ("aux", C.c_uint32),
    ]


class Reservoir(C.Structure):
    _fields_ = [
        ("id", Id),
        ("kind", C.c_uint16),
        ("zone", C.c_uint16),
        ("lo", Pos),
        ("hi", Pos),
        ("capacity", C.c_uint32),
        ("level", C.c_uint32),
        ("rate", C.c_uint32),
        ("reserved", C.c_uint32),
    ]


class Recipe(C.Structure):
    _fields_ = [
        ("ancestry", Id),
        ("seed", C.c_uint32),
        ("epoch", C.c_uint32),
        ("revision", C.c_uint32),
        ("crc", C.c_uint32),
        ("count", C.c_uint16),
        ("links_n", C.c_uint16),
        ("features_n", C.c_uint16),
        ("exceptions_n", C.c_uint16),
        ("reservoirs_n", C.c_uint16),
        ("modules", Module * 128),
        ("links", Link * 256),
        ("features", Feature * 8),
        ("exceptions", Exc * 24),
        ("reservoirs", Reservoir * 8),
    ]


def main():
    lib = C.CDLL(str(ROOT / "build/libsdk.so"))
    lib.ws_load.argtypes = [C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_materialize.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Module)]
    rng = random.Random(20260922)
    r = Recipe()
    vectors = []
    start = time.perf_counter()
    worlds = 0
    bad = 0
    for name in ("cascade", "elek_grid", "macro"):
        src = json.loads((ROOT / f"content/worlds/{name}.json").read_text())
        data, manifest = compile_recipe(src)
        for seed in range(1000):
            source = copy.deepcopy(src)
            source["seed"] = seed
            product, _ = compile_recipe(source)
            assert lib.ws_load(C.byref(r), product, len(product)) == 0
            generated = bytearray()
            for i, m in enumerate(source["modules"]):
                out = Module()
                lib.ws_materialize(C.byref(r), i, C.byref(out))
                j = h(seed ^ m.get("variation_seed", m["key"])) % 5 - 2
                expected = m["position"].copy()
                expected[0] += j * m.get("jitter", [0, 0])[0]
                expected[2] += j * m.get("jitter", [0, 0])[1]
                assert [out.pos.x, out.pos.y, out.pos.z] == expected
                assert list(out.id.word) == manifest["modules"][i]["id"]
                generated += bytes(out)
            if seed in (0, 1, 42, 999):
                vectors.append(
                    {
                        "recipe": name,
                        "seed": seed,
                        "product_sha256": hashlib.sha256(product).hexdigest(),
                        "materialized_sha256": hashlib.sha256(generated).hexdigest(),
                    }
                )
            worlds += 1
        for n in range(len(data)):
            assert lib.ws_load(C.byref(r), data[:n], n) != 0
            bad += 1
        for _ in range(1000):
            b = bytearray(data)
            i = rng.randrange(len(b))
            b[i] ^= 1 << rng.randrange(8)
            assert lib.ws_load(C.byref(r), bytes(b), len(b)) != 0
            bad += 1
        # Attacker recomputes CRC: semantic validation must still reject
        # invalid fields. Offsets are relative to the module table, which
        # starts at 48 (schema 1), 52 (schema 2, sparse features) or 54
        # (schema 3, reservoirs).
        schema = struct.unpack_from("<H", data, 4)[0]
        table = {1: 48, 2: 52, 3: 54}[schema]
        for offset, value in (
            (table + 16, 0),
            (table + 18, 255),
            (table + 20, 65535),
            (table + 22, 7),
            (table + 60, 1001),
        ):
            b = bytearray(data)
            struct.pack_into("<H", b, offset, value)
            struct.pack_into("<I", b, 12, zlib.crc32(b[16:]))
            assert lib.ws_load(C.byref(r), bytes(b), len(b)) != 0
            bad += 1
        # Reservoir records are validated fail-closed too: bad kind, zone,
        # degenerate region, empty capacity and overfull level all reject
        # even with a recomputed CRC. The reservoir table follows the
        # feature and exception tables in schema 3.
        if schema == 3 and src.get("reservoirs"):
            rtable = table + 64 * len(src["modules"]) + 8 * len(src["links"]) + 56 * len(src.get("features", [])) + 12 * sum(len(f.get("exceptions", [])) for f in src.get("features", []))
            for offset, value, pack in (
                (rtable + 16, 5, "<H"),  # unknown kind
                (rtable + 18, 4, "<H"),  # zone class out of range
                (rtable + 44, 0, "<I"),  # zero capacity
                (rtable + 48, 400001, "<I"),  # level beyond capacity (max 400000)
            ):
                b = bytearray(data)
                struct.pack_into(pack, b, offset, value)
                struct.pack_into("<I", b, 12, zlib.crc32(b[16:]))
                assert lib.ws_load(C.byref(r), bytes(b), len(b)) != 0
                bad += 1
            b = bytearray(data)
            hi_x = struct.unpack_from("<i", b, rtable + 32)[0]
            struct.pack_into("<i", b, rtable + 20, hi_x)  # lo.x == hi.x
            struct.pack_into("<I", b, 12, zlib.crc32(b[16:]))
            assert lib.ws_load(C.byref(r), bytes(b), len(b)) != 0
            bad += 1
        for mutation in ("tag", "phos", "cycle", "unconnected"):
            s = copy.deepcopy(src)
            if mutation == "tag":
                s["modules"][4]["tags"].append("Physics.Unknown")
            elif mutation == "phos":
                s["modules"][4]["phos"] = "Austentic"
            elif mutation == "cycle":
                s["modules"][0]["parent"] = s["modules"][0]["name"]
            else:
                s["links"] = []
            try:
                compile_recipe(s)
            except (ValueError, KeyError):
                bad += 1
            else:
                raise AssertionError(mutation)
        # Source-level reservoir rejections: the compiler mirrors the C
        # fail-closed rules instead of emitting bad products.
        if src.get("reservoirs"):
            for mutation in ("kind", "zone", "region", "capacity", "rate", "overlap"):
                s = copy.deepcopy(src)
                if mutation == "kind":
                    s["reservoirs"][0]["kind"] = "aether"
                elif mutation == "zone":
                    s["reservoirs"][0]["zone"] = 4
                elif mutation == "region":
                    s["reservoirs"][0]["lo"][0] = s["reservoirs"][0]["hi"][0] + 1
                elif mutation == "capacity":
                    s["reservoirs"][0]["level"] = s["reservoirs"][0]["capacity"] + 1
                elif mutation == "rate":
                    s["reservoirs"][0]["rate"] = 0
                else:
                    # Clone the first reservoir onto its own region: same-kind
                    # overlap is ambiguous for stage lookup and must reject.
                    first = s["reservoirs"][0]
                    s["reservoirs"].append({**first, "name": "overlap_probe", "key": first["key"] + 999})
                try:
                    compile_recipe(s)
                except ValueError:
                    bad += 1
                else:
                    raise AssertionError(mutation)
    result = {
        "status": "PASS",
        "worlds": worlds,
        "rejected_malformed_products": bad,
        "elapsed_ms": round((time.perf_counter() - start) * 1000, 3),
        "workspace_bytes": C.sizeof(Recipe),
        "module_bytes": C.sizeof(Module),
        "vectors": vectors,
        "limitations": [
            "Route costs are topology- and terrain-aware, but movement remains link-based: no continuous off-trail terrain traversal.",
            "No P4 runtime parity measurement.",
            "Three recipe families; not arbitrary world correctness.",
        ],
    }
    (ROOT / "results/worldsdk/generation.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    print(json.dumps(result))


if __name__ == "__main__":
    main()
