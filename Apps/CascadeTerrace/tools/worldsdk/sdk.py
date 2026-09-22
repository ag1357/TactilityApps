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
    for a, b, kind in links:
        a, b = ids[a], ids[b]
        k = {"walk": 0, "lift": 1, "portal": 2}[kind]
        if a == b or a not in graph or b not in graph:
            raise ValueError("illegal navigation endpoint")
        graph[a].add(b)
        graph[b].add(a)
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
