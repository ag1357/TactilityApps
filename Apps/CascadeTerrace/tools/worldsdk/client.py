#!/usr/bin/env python3
"""Interactive SDK multiplayer fixture. Uses the game's C software renderer.
WASD: move, arrows: turn, E: lift/portal, 1: extract, 2: repair, 3: transfer,
4: place a Hydro decoration in your room, Escape: exit. No PC/cloud cognition.
This desktop fixture is not the normal Cascade quest client or a P4 transport.
"""

import argparse, ctypes as C, ctypes.util, hashlib, json, math, pathlib, socket, struct, time
from server import Recipe, Address, Pos, ROOT


class Renderer(C.Structure):
    _fields_ = [
        ("pixels", C.c_uint16 * (240 * 160)),
        ("depth", C.c_uint16 * (240 * 160)),
        ("triangles", C.c_uint32),
        ("pixels_written", C.c_uint32),
        ("camera_x", C.c_float),
        ("camera_y", C.c_float),
        ("camera_z", C.c_float),
        ("yaw", C.c_float),
        ("pitch", C.c_float),
        ("conversation", C.c_int),
        ("frame", C.c_uint32),
    ]


class Metrics(C.Structure):
    _fields_ = [
        (n, C.c_uint32)
        for n in ("triangles", "vertices", "active", "materialized", "proxy")
    ]


def bind(lib, name, restype, args):
    f = getattr(lib, name)
    f.restype = restype
    f.argtypes = args
    return f


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("product")
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--headless", action="store_true")
    ap.add_argument("--frames", type=int, default=0)
    ap.add_argument("--auto-forward", action="store_true")
    ap.add_argument("--extract", action="store_true")
    ap.add_argument("--repair", action="store_true")
    ap.add_argument("--report")
    ap.add_argument("--wait-peers", type=int, default=0)
    a = ap.parse_args()
    lib = C.CDLL(str(ROOT / "build/libworldview.so"))
    bind(lib, "ws_load", C.c_int, [C.POINTER(Recipe), C.c_void_p, C.c_size_t])
    bind(
        lib,
        "render_world",
        None,
        [C.POINTER(Renderer), C.POINTER(Recipe), Address, C.c_int, C.POINTER(Metrics)],
    )
    bind(lib, "render_world_peer", None, [Pos])
    bind(lib, "render_world_marker", None, [Pos, C.c_uint32])
    bind(
        lib,
        "draw_panel",
        None,
        [C.POINTER(Renderer), C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint16],
    )
    bind(
        lib,
        "draw_text",
        None,
        [C.POINTER(Renderer), C.c_int, C.c_int, C.c_char_p, C.c_uint16, C.c_int],
    )
    recipe = Recipe()
    data = pathlib.Path(a.product).read_bytes()
    if lib.ws_load(C.byref(recipe), data, len(data)):
        raise ValueError("invalid product")
    sha = hashlib.sha256(data).hexdigest()
    sdl = C.CDLL(ctypes.util.find_library("SDL2-2.0"))
    for name, result, args in [
        ("SDL_Init", C.c_int, [C.c_uint32]),
        (
            "SDL_CreateWindow",
            C.c_void_p,
            [C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint32],
        ),
        ("SDL_CreateRenderer", C.c_void_p, [C.c_void_p, C.c_int, C.c_uint32]),
        (
            "SDL_CreateTexture",
            C.c_void_p,
            [C.c_void_p, C.c_uint32, C.c_int, C.c_int, C.c_int],
        ),
        ("SDL_UpdateTexture", C.c_int, [C.c_void_p, C.c_void_p, C.c_void_p, C.c_int]),
        ("SDL_RenderCopy", C.c_int, [C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]),
        ("SDL_RenderPresent", None, [C.c_void_p]),
        ("SDL_PollEvent", C.c_int, [C.c_void_p]),
        ("SDL_GetKeyboardState", C.POINTER(C.c_uint8), [C.c_void_p]),
        ("SDL_DestroyTexture", None, [C.c_void_p]),
        ("SDL_DestroyRenderer", None, [C.c_void_p]),
        ("SDL_DestroyWindow", None, [C.c_void_p]),
        ("SDL_Quit", None, []),
    ]:
        bind(sdl, name, result, args)
    if a.headless:
        import os

        os.environ["SDL_VIDEODRIVER"] = "dummy"
    if sdl.SDL_Init(0x21):
        raise RuntimeError("SDL init failed")
    window = sdl.SDL_CreateWindow(
        b"World SDK multiplayer fixture", 100, 100, 960, 640, 0
    )
    display = sdl.SDL_CreateRenderer(window, -1, 1)
    texture = sdl.SDL_CreateTexture(display, 0x15151002, 1, 240, 160)
    if not window or not display or not texture:
        raise RuntimeError("SDL resource creation failed")
    sock = socket.create_connection(("127.0.0.1", a.port), timeout=5)
    stream = sock.makefile("rwb")
    wire_sent = wire_received = 0
    world_cache = None

    def request(msg):
        nonlocal wire_sent, wire_received, world_cache
        packet = json.dumps(msg, separators=(",", ":")).encode() + b"\n"
        stream.write(packet)
        stream.flush()
        raw = stream.readline(65537)
        wire_sent += len(packet)
        wire_received += len(raw)
        v = json.loads(raw)
        if "world" in v:
            world_cache = v["world"]
        elif world_cache and v.get("world_revision") == world_cache["revision"]:
            v["world"] = world_cache
        else:
            raise RuntimeError(v.get("error", "replica revision mismatch"))
        return v

    state = request({"cmd": "hello", "schema": 1})
    state.pop("token", None)
    if state["world"]["recipe_sha256"] != sha:
        raise ValueError("client/server product mismatch")
    deadline = time.monotonic() + 5
    while len(state["movement"]) < a.wait_peers and time.monotonic() < deadline:
        time.sleep(0.02)
        state = request({"cmd": "snapshot"})
    identity = state["you"]["id"]
    frame = Renderer()
    metrics = Metrics()
    event = C.create_string_buffer(56)
    yaw = 0
    frames = 0
    render_ms = 0.0
    actions = []
    peers_seen = set()
    running = True

    def action(kind, flag, amount=1):
        nonlocal state
        p = state["movement"][str(identity)]
        candidates = []
        for i, m in enumerate(recipe.modules[: recipe.count]):
            if m.flags & flag:
                distance = (
                    (m.pos.x - p["x"]) ** 2
                    + (m.pos.y - p["y"]) ** 2
                    + (m.pos.z - p["z"]) ** 2
                )
                candidates.append((distance, i))
        if not candidates:
            return
        target = min(candidates)[1]
        if kind == "DECORATE":
            owned = [
                i
                for i, e in enumerate(state["world"]["entities"])
                if e["owner"] == identity
            ]
            if not owned:
                return
            target = owned[0]
        e = state["world"]["entities"][target]
        other = next((int(k) for k in state["movement"] if int(k) != identity), 0)
        state = request(
            {
                "cmd": "act",
                "action": kind,
                "target": target,
                "sequence": state["you"]["sequence"] + 1,
                "epoch": e["epoch"],
                "base_revision": e["revision"],
                "amount": amount,
                "aux": other if kind == "TRANSFER" else 0,
                "recipe_sha256": sha,
            }
        )
        actions.append({"action": kind, "status": state["status"]})

    try:
        while running:
            while sdl.SDL_PollEvent(event):
                typ = struct.unpack_from("<I", event.raw)[0]
                if typ == 0x100:
                    running = False
                if typ == 0x300:
                    key = struct.unpack_from("<i", event.raw, 20)[0]
                    if key == 27:
                        running = False
                    elif key == ord("e"):
                        state = request({"cmd": "use"})
                    elif key == ord("1"):
                        action("EXTRACT", 32, 5)
                    elif key == ord("2"):
                        action("REPAIR", 4)
                    elif key == ord("3"):
                        action("TRANSFER", 32)
                    elif key == ord("4"):
                        action("DECORATE", 8, 2)
            if frames == 0 and a.extract:
                action("EXTRACT", 32, 5)
            if frames == 1 and a.repair:
                action("REPAIR", 4)
            keys = sdl.SDL_GetKeyboardState(None)
            yaw = (yaw + 3 * (keys[79] - keys[80])) % 360
            angle = yaw * math.pi / 180
            forward = keys[26] - keys[22] or int(a.auto_forward)
            side = keys[7] - keys[4]
            state = request(
                {
                    "cmd": "move",
                    "dx": int(
                        80 * (forward * math.sin(angle) + side * math.cos(angle))
                    ),
                    "dz": int(
                        80 * (forward * math.cos(angle) - side * math.sin(angle))
                    ),
                }
            )
            p = state["movement"][str(identity)]
            at = Address(Pos(p["x"], p["y"], p["z"]), p["scope"])
            t = time.perf_counter()
            lib.render_world(C.byref(frame), C.byref(recipe), at, yaw, C.byref(metrics))
            for k, peer in state["movement"].items():
                if int(k) != identity and peer["scope"] == at.scope:
                    lib.render_world_peer(Pos(peer["x"], peer["y"], peer["z"]))
                    peers_seen.add(int(k))
            if at.scope != 65535:
                e = state["world"]["entities"][at.scope]
                m = recipe.modules[at.scope]
                for slot, item in enumerate(e["decor"]):
                    if item:
                        lib.render_world_marker(
                            Pos(
                                m.pos.x + (-2000 if slot & 1 else 2000),
                                m.pos.y,
                                m.pos.z + (-2000 if slot & 2 else 2000),
                            ),
                            0x66BBCC,
                        )
            render_ms += (time.perf_counter() - t) * 1000
            lib.draw_panel(C.byref(frame), 0, 0, 240, 22, 0)
            hud = f"P{identity} | {state['you']['credits']} CREDIT | R{state['world']['revision']}"
            lib.draw_text(C.byref(frame), 2, 1, hud.encode(), 0xFFFF, 0)
            lib.draw_text(
                C.byref(frame), 2, 11, b"1 GATHER 2 REPAIR 3 GIVE E ENTER", 0xFFFF, 0
            )
            sdl.SDL_UpdateTexture(texture, None, frame.pixels, 480)
            sdl.SDL_RenderCopy(display, texture, None, None)
            sdl.SDL_RenderPresent(display)
            frames += 1
            if a.frames and frames >= a.frames:
                running = False
            if not a.headless:
                time.sleep(0.016)
        result = {
            "frames": frames,
            "player": identity,
            "peers_seen": sorted(peers_seen),
            "mean_render_ms": render_ms / frames,
            "wire_sent": wire_sent,
            "wire_received": wire_received,
            "public_hash": state["public_hash"],
            "actions": actions,
            "final_position": p,
            "physical": False,
        }
        if a.report:
            pathlib.Path(a.report).write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result))
    finally:
        stream.close()
        sock.close()
        sdl.SDL_DestroyTexture(texture)
        sdl.SDL_DestroyRenderer(display)
        sdl.SDL_DestroyWindow(window)
        sdl.SDL_Quit()


if __name__ == "__main__":
    main()
