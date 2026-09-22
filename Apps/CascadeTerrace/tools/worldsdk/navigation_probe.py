#!/usr/bin/env python3
"""Attempt each declared walk edge using actual swept collision movement.
A failing declared edge blocks promotion of the SDK settlement traversal mode.
The legacy game uses its separately validated generation-v1 terrain routes.
"""

import ctypes as C, json, math
from qualify import ROOT, Recipe, Module
from server import Address, Pos, library
from sdk import compile_recipe


def main():
    lib = library()
    lib.ws_materialize.argtypes = [C.POINTER(Recipe), C.c_uint16, C.POINTER(Module)]
    rows = []
    for name in ("cascade", "elek_grid"):
        source = json.loads((ROOT / f"content/worlds/{name}.json").read_text())
        data, manifest = compile_recipe(source)
        r = Recipe()
        assert lib.ws_load(C.byref(r), data, len(data)) == 0
        for l in r.links[: r.links_n]:
            if l.kind:
                continue
            a = Module()
            b = Module()
            lib.ws_materialize(C.byref(r), l.a, C.byref(a))
            lib.ws_materialize(C.byref(r), l.b, C.byref(b))
            at = Address(Pos(a.pos.x, a.pos.y + 300, a.pos.z), 65535)
            passed = False
            for step in range(10000):
                dx, dz = b.pos.x - at.pos.x, b.pos.z - at.pos.z
                distance = math.hypot(dx, dz)
                if distance < 150:
                    passed = True
                    break
                dx, dz = int(dx / distance * 100), int(dz / distance * 100)
                if not lib.ws_move(C.byref(r), C.byref(at), dx, dz):
                    break
            rows.append(
                {
                    "recipe": name,
                    "from": manifest["modules"][l.a]["name"],
                    "to": manifest["modules"][l.b]["name"],
                    "pass": passed,
                    "steps": step,
                    "stop": [at.pos.x, at.pos.y, at.pos.z],
                }
            )
    result = {
        "status": "PASS" if all(r["pass"] for r in rows) else "FAILED",
        "passed": sum(r["pass"] for r in rows),
        "tested": len(rows),
        "gate": "SDK traversal production promotion blocked when any declared edge fails",
        "method": "Direct walk edges, 100 mm swept steps using runtime ground and collision. Not a search for alternate routes.",
        "rows": rows,
    }
    (ROOT / "results/worldsdk/navigation.json").write_text(
        json.dumps(result, indent=2) + "\n"
    )
    print(json.dumps({k: v for k, v in result.items() if k != "rows"}))
    return result["status"] != "PASS"


if __name__ == "__main__":
    raise SystemExit(main())
