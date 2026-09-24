#!/usr/bin/env python3
"""Two OS client processes, real TCP server, reconnect and disk restart checks.

Phase 1 (cascade product): entity operations, movement, public replica
convergence, reconnect and a terminated-and-restarted server.

Phase 2 (macro product, Gate 4): two clients walk the derived trail to the
anomaly gate's two ends (one takes the winze lift up the karst flank), cross
the open gate in both directions, then one depletes the anchored Phos lode
through the regional op path. Both clients observe the shortcut removed
live (use denied at the far end), converge on the same public replica and
the same derived gate state, reconnect with their resume tokens, and a
terminated-and-restarted server restores the same closed gate from the
persisted canonical state. Recharge-driven reopening is proven against
ticks in the C and Python suites (the fixture server has no clock)."""

import ctypes as C
import json
import math
import multiprocessing as mp
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import Module, Recipe, ROOT
from sdk import link_open, runtime_view


def worker(pipe, port):
    token = None
    sock = None
    stream = None
    world_cache = None

    def connect():
        nonlocal sock, stream, token, world_cache
        sock = socket.create_connection(("127.0.0.1", port), timeout=20)
        stream = sock.makefile("rwb")
        stream.write(
            json.dumps({"cmd": "hello", "schema": 1, "token": token}).encode() + b"\n"
        )
        stream.flush()
        v = json.loads(stream.readline())
        token = v.pop("token", token)
        world_cache = v["world"]
        return v

    try:
        pipe.send(connect())
        while True:
            cmd = pipe.recv()
            if cmd == "stop":
                break
            if cmd == "reconnect":
                stream.close()
                sock.close()
                time.sleep(0.05)
                reply = connect()
            else:
                stream.write(json.dumps(cmd).encode() + b"\n")
                stream.flush()
                reply = json.loads(stream.readline())
            if "world" in reply:
                world_cache = reply["world"]
            elif reply.get("world_revision") == world_cache["revision"]:
                reply["world"] = world_cache
            pipe.send(reply)
    finally:
        if stream:
            stream.close()
        if sock:
            sock.close()
        pipe.close()


def main():
    traces = []
    checks = 0

    def check(value):
        nonlocal checks
        checks += 1
        if not value:
            raise AssertionError(f"check {checks}")

    with tempfile.TemporaryDirectory(prefix="ct-net-") as temp:
        def start(product, save_dir):
            p = subprocess.Popen(
                [
                    sys.executable,
                    str(ROOT / "tools/worldsdk/server.py"),
                    str(ROOT / product),
                    "--save",
                    save_dir,
                    "--port",
                    "0",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
            ready = json.loads(p.stdout.readline())
            return p, ready["port"]

        def phase(product, save_dir, on_ready):
            """One server, two worker clients; on_ready receives the
            (send, snapshot, position) helpers once both clients joined."""
            server, port = start(product, save_dir)
            clients = []
            tag = pathlib.Path(product).stem
            try:
                for _ in range(2):
                    parent, child = mp.Pipe()
                    p = mp.Process(target=worker, args=(child, port))
                    p.start()
                    clients.append((p, parent))
                    v = parent.recv()
                    check(v["ok"])

                def send(index, msg, quiet=False):
                    pipe = clients[index][1]
                    pipe.send(msg)
                    v = pipe.recv()
                    if not quiet:
                        traces.append(
                            {
                                "client": index,
                                "phase": tag,
                                "request": msg.get("cmd") if isinstance(msg, dict) else msg,
                                "status": v.get("status"),
                                "public_hash": v.get("public_hash"),
                                "revision": v.get("world", {}).get("revision"),
                            }
                        )
                    return v

                def snapshot(i):
                    return send(i, {"cmd": "snapshot"})

                def position(i, v=None):
                    v = v or snapshot(i)
                    you = str(v["you"]["id"])
                    return v["movement"][you]

                on_ready(send, snapshot, position)
            finally:
                for p, pipe in clients:
                    if p.is_alive():
                        pipe.send("stop")
                        p.join(5)
                        check(p.exitcode == 0)
                server.terminate()
                server.wait(5)

        # ---- phase 1: the cascade product (entity ops, convergence) ----
        from server import World

        cascade_report = {}

        def cascade_ready(send, snapshot, position):
            def act(i, action, target, amount=1, aux=0, sequence=None):
                v = snapshot(i)
                e = v["world"]["entities"][target]
                return send(
                    i,
                    {
                        "cmd": "act",
                        "action": action,
                        "target": target,
                        "sequence": sequence
                        if sequence is not None
                        else v["you"]["sequence"] + 1,
                        "epoch": e["epoch"],
                        "base_revision": e["revision"],
                        "amount": amount,
                        "aux": aux,
                        "recipe_sha256": v["world"]["recipe_sha256"],
                    },
                )

            check(act(0, "EXTRACT", 16, 10)["ok"])
            check(act(1, "EXTRACT", 16, 5)["ok"])
            v = act(0, "REPAIR", 4)
            check(
                v["ok"]
                and v["you"]["credits"] == 10
                and v["world"]["entities"][4]["health"] == 100
            )
            check(not act(1, "REPAIR", 4)["ok"])
            v = act(0, "TRANSFER", 16, 2, 102)
            check(v["ok"] and v["you"]["inventory"][1] == 7)
            check(snapshot(1)["you"]["inventory"][1] == 7)
            before = v["public_hash"]
            check(
                not act(0, "TRANSFER", 16, 2, 102, sequence=v["you"]["sequence"])["ok"]
            )
            check(snapshot(0)["public_hash"] == before)
            check(not act(0, "DAMAGE", 4)["ok"])
            for _ in range(20):
                check(send(0, {"cmd": "move", "dx": 0, "dz": 100}, quiet=True)["ok"])
            for _ in range(10):
                check(send(1, {"cmd": "move", "dx": 100, "dz": 0}, quiet=True)["ok"])
            v = snapshot(0)
            check(v["movement"]["101"] != v["movement"]["102"])
            check("relations" not in v["world"])
            check(v["public_hash"] == snapshot(1)["public_hash"])
            check(send(0, "reconnect")["public_hash"] == v["public_hash"])
            cascade_report["converged_hash"] = v["public_hash"]

        phase("build/cascade.cws", str(pathlib.Path(temp) / "world"), cascade_ready)
        # Persisted canonical state survives a terminated and restarted server.
        restored = World(ROOT / "build/cascade.cws", str(pathlib.Path(temp) / "world"))
        check(restored.snapshot(101)["public_hash"] == cascade_report["converged_hash"])
        cascade_report["restart_hash_match"] = True

        # ---- phase 2: the macro product (the Gate 4 anomaly scenario) ----
        macro_report = {}
        macro_save = str(pathlib.Path(temp) / "macro-world")

        # Independent C view of the committed product: waypoints and routes.
        lib = World(str(ROOT / "build/macro.cws"), macro_save).lib
        data = (ROOT / "build/macro.cws").read_bytes()
        recipe = Recipe()
        assert lib.ws_load(C.byref(recipe), data, len(data)) == 0
        names = [
            "mountain", "shoulder_east", "shoulder_west", "watershed", "cave",
            "karst_ridge", "dam_hold", "plains", "forest", "wetland", "ruin",
            "high_gate", "trail_e0", "trail_e1", "trail_e2", "trail_e3",
            "trail_e4", "trail_ford_e", "trail_ford_w", "trail_w1",
            "ford_haven", "phos_ruin",
        ]
        ids = {n: i for i, n in enumerate(names)}
        centers = {}
        for i, name in enumerate(names):
            out = Module()
            lib.ws_materialize(C.byref(recipe), i, C.byref(out))
            centers[name] = (out.pos.x, out.pos.y, out.pos.z)
        cost = C.c_uint64()
        path_arr = (C.c_uint16 * 128)()

        def walk_path(a, b):
            n = lib.ws_route_cost(C.byref(recipe), a, b, 1, C.byref(cost), path_arr, 128)
            assert n > 0
            return [names[i] for i in path_arr[:n]]

        src = json.loads((ROOT / "content/worlds/macro.json").read_text())
        _, gate_links, _, gate_recs = runtime_view(src)
        gate_i = next(k for k, l in enumerate(gate_links) if l[2] == 3)

        def gate_open_from(v):
            """A client derives gate state from the public replica through
            the same pure function the server applies."""
            st = {"level": v["world"]["reservoirs"]["level"]}
            return link_open(gate_links, gate_recs, st, gate_i)

        def macro_ready(send, snapshot, position):
            def walk_to(i, target_name):
                route = walk_path(ids["watershed"], ids[target_name])
                for name in route[1:]:
                    tx, _, tz = centers[name]
                    for _ in range(4000):
                        p = position(i)
                        dx, dz = tx - p["x"], tz - p["z"]
                        dist = math.hypot(dx, dz)
                        if dist < 150:
                            break
                        # long approaches take 1000 mm command steps (the
                        # engine sweeps them in 100 mm substeps); the final
                        # approach drops to the probe's 100 mm steps so the
                        # arrival window cannot be overshot
                        m = max(abs(dx), abs(dz), 1)
                        step = 1000 if dist > 2000 else 100
                        v = send(
                            i,
                            {"cmd": "move", "dx": int(dx * step / m), "dz": int(dz * step / m)},
                            quiet=True,
                        )
                        check(v["ok"])
                    else:
                        raise AssertionError(f"client {i} could not reach {name}")

            def use(i):
                return send(i, {"cmd": "use"})

            def ract(i, action, target, amount, aux):
                """Regional ops bind to the global revision and carry epoch 1."""
                v = snapshot(i)
                return send(
                    i,
                    {
                        "cmd": "act",
                        "action": action,
                        "target": target,
                        "sequence": v["you"]["sequence"] + 1,
                        "epoch": 1,
                        "base_revision": v["world"]["revision"],
                        "amount": amount,
                        "aux": aux,
                        "recipe_sha256": v["world"]["recipe_sha256"],
                    },
                )

            # both clients spawn at the watershed; A walks to the winze foot
            # and rides the lift up to the gate seat, B walks to the ruin
            spawn = position(0)
            check((spawn["x"], spawn["z"]) == (centers["watershed"][0], centers["watershed"][2]))
            check((position(1)["x"], position(1)["z"]) == (spawn["x"], spawn["z"]))
            walk_to(0, "trail_e3")
            check(use(0)["ok"])  # the winze lift starts (timed travel)
            for _ in range(400):
                if (lambda p: (p["x"], p["y"], p["z"]) == centers["phos_ruin"])(position(0)):
                    break
                check(send(0, {"cmd": "move", "dx": 0, "dz": 0}, quiet=True)["ok"])
            else:
                raise AssertionError("the lift never delivered client A")
            walk_to(1, "ruin")
            p1 = position(1)
            cx, cy, cz = centers["ruin"]
            check(math.hypot(p1["x"] - cx, p1["z"] - cz) < 150 and abs(p1["y"] - cy) <= 300)

            # the open gate crosses in both directions, server-authoritative
            v = snapshot(0)
            check(v["world"]["reservoirs"]["level"][3] == 160000)
            check(gate_open_from(v) == 1)
            check(use(1)["ok"])  # B crosses ruin -> phos_ruin
            p1 = position(1)
            check((p1["x"], p1["y"], p1["z"]) == centers["phos_ruin"])
            check(use(0)["ok"])  # A crosses phos_ruin -> ruin
            p0 = position(0)
            check((p0["x"], p0["y"], p0["z"]) == centers["ruin"])
            check(snapshot(0)["public_hash"] == snapshot(1)["public_hash"])

            # B, standing on the anchored lode, depletes it through the
            # regional op path until the gate closes below half stock
            check(ract(1, "EXCAVATE", 3, 65535, 1)["ok"])
            v = snapshot(1)
            check(v["world"]["reservoirs"]["level"][3] == 94465)
            check(gate_open_from(v) == 1)  # still above half
            check(ract(1, "CONVERT", 3, 65535, 1)["ok"])
            check(ract(1, "EXCAVATE", 3, 65535, 1)["ok"])
            v = snapshot(1)
            check(v["world"]["reservoirs"]["level"][3] == 28930)
            check(gate_open_from(v) == 0)  # the shortcut is gone

            # A observes the removal live: the far-end seat has no other
            # non-walk link, so use simply fails there
            check(not use(0)["ok"])
            # both clients converge on the same replica and derived state
            v0, v1 = snapshot(0), snapshot(1)
            check(v0["public_hash"] == v1["public_hash"])
            check(gate_open_from(v0) == gate_open_from(v1) == 0)
            # B, at the gate seat, falls back to the ordinary winze lift
            check(use(1)["ok"])
            # reconnect with the resume token: same canonical state
            check(send(0, "reconnect")["public_hash"] == v0["public_hash"])
            check(not use(0)["ok"])
            macro_report["depleted_level"] = 28930
            macro_report["convergence"] = True
            macro_report["crossed_both_directions"] = True
            return v0["public_hash"]

        holder = {}

        def macro_wrapped(send, snapshot, position):
            holder["hash"] = macro_ready(send, snapshot, position)

        phase("build/macro.cws", macro_save, macro_wrapped)
        check(holder["hash"])
        # a terminated and restarted server restores the closed gate from
        # the persisted canonical state
        restored = World(ROOT / "build/macro.cws", macro_save)
        snap = restored.snapshot(restored.state.players[0].id)
        check(snap["world"]["reservoirs"]["level"][3] == 28930)
        st = {"level": snap["world"]["reservoirs"]["level"]}
        check(link_open(gate_links, gate_recs, st, gate_i) == 0)
        macro_report["restart_level"] = snap["world"]["reservoirs"]["level"][3]
        macro_report["restart_derived_closed"] = True

        # ---- phase 3: the social product (Gate 6 canonical events) ----
        # Two clients over real TCP: one extracts at the field, walks to the
        # station yard and speaks privately to kyra (show, tell: committed
        # canonical events that never touch the public feed), reconnects and
        # replays the exact command (the receipt answers, nothing re-charged),
        # then walks the declared route to the market and files a formal
        # report with the guild clerk. The report is not news until the
        # authority clock passes the dispatch delay; the test advances the
        # persisted canonical clock between server generations (the fixture
        # server has no clock), and a fresh server publishes the faction's
        # receipt to every client through the public replica.
        social_report = {}
        social_save = str(pathlib.Path(temp) / "social-world")
        slib = World(str(ROOT / "build/social.cws"), social_save).lib
        sdata = (ROOT / "build/social.cws").read_bytes()
        srecipe = Recipe()
        assert slib.ws_load(C.byref(srecipe), sdata, len(sdata)) == 0
        snames = [m["name"] for m in json.loads((ROOT / "content/worlds/social.json").read_text())["modules"]]
        sids = {n: i for i, n in enumerate(snames)}
        scenters = {}
        for i in range(srecipe.count):
            out = Module()
            slib.ws_materialize(C.byref(srecipe), i, C.byref(out))
            scenters[snames[i]] = (out.pos.x, out.pos.y, out.pos.z)
        scost = C.c_uint64()
        spath = (C.c_uint16 * 128)()

        def swalk_route(send, position, i, a, b):
            n = slib.ws_route_cost(C.byref(srecipe), a, b, 1, C.byref(scost), spath, 128)
            assert n > 0
            route = [snames[k] for k in spath[:n]]
            for name in route:  # include the start: the client may stand off-route
                tx, _, tz = scenters[name]
                for _ in range(4000):
                    p = position(i)
                    dx, dz = tx - p["x"], tz - p["z"]
                    dist = math.hypot(dx, dz)
                    if dist < 150:
                        break
                    m = max(abs(dx), abs(dz), 1)
                    step = 1000 if dist > 2000 else 100
                    check(send(i, {"cmd": "move", "dx": int(dx * step / m), "dz": int(dz * step / m)}, quiet=True)["ok"])
                else:
                    raise AssertionError(f"client {i} could not reach {name}")

        def social_ready(send, snapshot, position):
            def sact(i, action, target, amount=0, aux=0, sequence=None):
                v = snapshot(i)
                e = v["world"]["entities"][target]
                return send(
                    i,
                    {
                        "cmd": "act",
                        "action": action,
                        "target": target,
                        "sequence": sequence
                        if sequence is not None
                        else v["you"]["sequence"] + 1,
                        "epoch": e["epoch"],
                        "base_revision": e["revision"],
                        "amount": amount,
                        "aux": aux,
                        "recipe_sha256": v["world"]["recipe_sha256"],
                    },
                )

            spawn = position(0)
            check((spawn["x"], spawn["z"]) == (scenters["station"][0], scenters["station"][2]))
            # extract at the hydro field, then walk out of the station's
            # south entrance to the yard beside kyra
            check(sact(0, "EXTRACT", sids["hydro_field"], 10)["ok"])
            check(snapshot(0)["you"]["inventory"][1] == 10)
            for _ in range(15):
                check(send(0, {"cmd": "move", "dx": 0, "dz": 1000}, quiet=True)["ok"])
            p0 = position(0)
            ky = scenters["kyra"]
            check(abs(p0["x"] - ky[0]) <= 3000 and abs(p0["z"] - ky[2]) <= 3000 and abs(p0["y"] - ky[1]) <= 3000)
            # private speech: committed canonical events, never public news
            feed_before = len(snapshot(1)["world"]["feed"])
            rev_before = snapshot(1)["world"]["revision"]
            check(sact(0, "TELL", sids["kyra"], 0, 18)["ok"])
            vshow = sact(0, "SHOW", sids["kyra"], 1, 1)
            check(vshow["ok"])
            show_seq = vshow["you"]["sequence"]  # the last committed command
            v1 = snapshot(1)
            check(len(v1["world"]["feed"]) == feed_before)  # nothing published
            check(v1["world"]["revision"] == rev_before + 2)  # but both committed
            # reconnect and replay the exact command: the receipt answers
            check(send(0, "reconnect")["ok"])
            v = sact(0, "SHOW", sids["kyra"], 1, 1, sequence=show_seq)
            check(v["ok"] and v["world"]["revision"] == rev_before + 2)  # replay
            check(v["you"]["inventory"][1] == 10)
            # walk the declared route to the market and file with the clerk
            swalk_route(send, position, 0, sids["station"], sids["market"])
            for _ in range(13):
                check(send(0, {"cmd": "move", "dx": -1000, "dz": 0}, quiet=True)["ok"])
            clerk = scenters["clerk"]
            p0 = position(0)
            check(abs(p0["x"] - clerk[0]) <= 3000 and abs(p0["z"] - clerk[2]) <= 3000)
            v = sact(0, "REPORT", sids["clerk"], 0, 18)
            check(v["ok"])
            # the report is filed but not yet news: the dispatch delay holds
            for c in (0, 1):
                feeds = snapshot(c)["world"]["feed"]
                check(all(f["kind"] != 5 for f in feeds))
            v0, v1 = snapshot(0), snapshot(1)
            check(v0["public_hash"] == v1["public_hash"])
            social_report["filed_revision"] = v0["world"]["revision"]
            social_report["private_feed_untouched"] = True
            social_report["receipt_replay"] = True

        phase("build/social.cws", social_save, social_ready)

        # The authority clock passes the dispatch delay between server
        # generations (the fixture server has no clock; this is the same
        # authority touch the C and Python suites drive directly).
        delivered = World(ROOT / "build/social.cws", social_save)
        check(delivered.state.pend_count == 1)
        delivered.lib.ws_clock_advance(
            C.byref(delivered.state), C.byref(delivered.recipe), 3600
        )
        check(delivered.state.pend_count == 0)
        notice = [f for f in delivered.state.feed[: delivered.state.feed_count] if f.kind == 5]
        check(len(notice) == 1 and notice[0].target == sids["clerk"])
        delivered.checkpoint()

        def social_after(send, snapshot, position):
            # a fresh server generation publishes the faction's receipt
            v0, v1 = snapshot(0), snapshot(1)
            feeds = v0["world"]["feed"]
            check(any(f["kind"] == 5 and f["target"] == sids["clerk"] for f in feeds))
            check(v0["public_hash"] == v1["public_hash"])
            social_report["delivered_notice"] = True
            social_report["delivery_revision"] = notice[0].revision

        phase("build/social.cws", social_save, social_after)

        report = {
            "status": "PASS",
            "checks": checks,
            "clients": 2,
            "transport": "TCP loopback; separate OS client processes",
            "server_restart": True,
            "public_replica_convergence": True,
            "private_relationships_sent": False,
            "anomaly_gate": macro_report,
            "social_boundary": social_report,
            "traces": traces,
            "limitations": [
                "No P4 network transport qualification",
                "No Internet deployment: plaintext transport and movement rate limits pending",
                "Offline merge verified by C suite; network branch upload is not implemented",
                "Clients are protocol harnesses, not integrated game clients",
                "Fixture server has no clock: the Gate 6 dispatch delay is advanced by the test between server generations through the same authority touch the C and Python suites drive",
            ],
        }
        (ROOT / "results/worldsdk/network.json").write_text(
            json.dumps(report, indent=2) + "\n"
        )
        print(json.dumps({k: v for k, v in report.items() if k != "traces"}))


if __name__ == "__main__":
    main()
