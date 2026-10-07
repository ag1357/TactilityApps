#!/usr/bin/env python3
"""Actual TCP synthetic clients; dense shared spawn is deliberately worst-case.
No server fixture-ID branches. Limits are measurements, not player caps.
"""
import argparse
import asyncio
import json
import pathlib
import signal
import struct
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from multiplayer import protocol as wire


async def read(reader):
    kind, count = wire.unpack_header(await reader.readexactly(12))
    return kind, await reader.readexactly(count)


async def client(port, stop, ready, state):
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(wire.frame(wire.HELLO, wire.AUTH.pack(1, bytes(16), bytes(32))))
    await writer.drain()
    kind, welcome = await read(reader)
    if kind != wire.WELCOME:
        raise ValueError("welcome")
    state["connected"] += 1
    ready.set()
    sequence = 0
    acknowledged = 0

    async def drain():
        nonlocal acknowledged
        while not stop.is_set():
            kind, payload = await read(reader)
            if kind == wire.SNAPSHOT:
                fields = wire.SNAP.unpack(payload[:wire.SNAP.size])
                acknowledged = fields[0]
                if fields[5] > 32:
                    raise ValueError("unbounded replica")
                state["snapshots"] += 1

    task = asyncio.create_task(drain())
    try:
        while not stop.is_set():
            if task.done():
                await task  # propagate reader/framing failures into client count
            if sequence - acknowledged >= 32:
                await asyncio.sleep(.02)
                continue
            sequence += 1
            # Real normal input shape; neutral dense crowd isolates fanout
            # cost from a terrain/path distribution benchmark.
            writer.write(wire.frame(wire.INPUT, wire.STEP.pack(sequence, 0, 0, 180, 0, 0)))
            await writer.drain()
            await asyncio.sleep(.02)
    finally:
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)
        writer.close()
        await writer.wait_closed()


async def checkpoint(count, seconds, results):
    with tempfile.TemporaryDirectory(prefix="mp-load-", dir=ROOT / "build") as temporary:
        work = pathlib.Path(temporary)
        server = await asyncio.create_subprocess_exec(
            sys.executable, "-u", "-m", "multiplayer.server", "--database", str(work / "world.sqlite"),
            "--port", "0", "--dev-enrolment", "--metrics", str(work / "metrics.json"),
            cwd=ROOT, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        tasks = []
        try:
            ready = json.loads(await asyncio.wait_for(server.stdout.readline(), 15))
            stop, started = asyncio.Event(), asyncio.Event()
            state = {"connected": 0, "snapshots": 0}
            tasks = [asyncio.create_task(client(ready["port"], stop, started, state)) for _ in range(count)]
            deadline = time.monotonic() + 40
            while state["connected"] < count and time.monotonic() < deadline:
                if any(t.done() for t in tasks):
                    raise RuntimeError("client failed during enrolment")
                await asyncio.sleep(.05)
            if state["connected"] != count:
                raise TimeoutError("enrolment deadline")
            await asyncio.sleep(seconds)
            stop.set()
            outcomes = await asyncio.wait_for(asyncio.gather(*tasks, return_exceptions=True), 15)
            failures = sum(isinstance(o, BaseException) for o in outcomes)
            server.send_signal(signal.SIGTERM)
            stdout, stderr = await asyncio.wait_for(server.communicate(), 30)
            if server.returncode:
                raise RuntimeError(stderr.decode())
            report = json.loads((work / "metrics.json").read_text())
            report.update({"requested_clients": count, "client_failures": failures,
                           "snapshots_consumed": state["snapshots"],
                           "input_flow_control": "at most 32 unacknowledged sequenced inputs",
                           "distribution": "dense colocated neutral-input TCP clients"})
            (results / f"load-{count}.json").write_text(json.dumps(report, indent=2) + "\n")
            print(json.dumps(report), flush=True)
            return report
        finally:
            for task in tasks:
                if not task.done():
                    task.cancel()
            if tasks:
                await asyncio.gather(*tasks, return_exceptions=True)
            if server.returncode is None:
                server.terminate()
                await server.wait()


async def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--counts", default="64,128,256,512,1000")
    ap.add_argument("--seconds", type=float, default=4)
    args = ap.parse_args()
    results = ROOT / "results/multiplayer"
    results.mkdir(exist_ok=True)
    for count in [int(v) for v in args.counts.split(",")]:
        report = await checkpoint(count, args.seconds, results)
        if report["client_failures"] or report["authority_loop_work_us"]["p99"] > 1000000:
            print("Stopped at measured failures/one-second loop work; no capacity claim.", flush=True)
            break


if __name__ == "__main__":
    asyncio.run(main())
