"""Real normal C client through a fault-injection TCP proxy, not runtime IDs."""
import asyncio
import json
import os
import pathlib
import struct
import tempfile
import time
import types
import unittest

from multiplayer import protocol as wire
from multiplayer.server import Server

ROOT = pathlib.Path(__file__).resolve().parents[1]


class NetworkClient(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=ROOT / "build")
        self.work = pathlib.Path(self.temporary.name)
        self.server = Server(types.SimpleNamespace(
            database=self.work / "world.sqlite", seed=42, interest_radius=30000, peer_limit=32,
            inference_endpoint="mock", idle_timeout=15, dev_enrolment=True, session_ttl=3600,
            replication_ms=100, checkpoint_seconds=30))
        self.listener = await asyncio.start_server(self.server.connect, "127.0.0.1", 0)
        self.port = self.listener.sockets[0].getsockname()[1]
        self.loop = asyncio.create_task(self.server.loop())
        self.proxy_tasks, self.writers = set(), set()
        self.drop_receipt = False
        self.corrupt_snapshot = False
        self.block_connections = False
        self.repairs = []
        self.proxy = await asyncio.start_server(self.forward, "127.0.0.1", 0)
        self.proxy_port = self.proxy.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.proxy.close()
        self.listener.close()
        await self.proxy.wait_closed()
        await self.listener.wait_closed()
        for writer in list(self.writers) + list(self.server.connections.values()):
            writer.close()
        if self.proxy_tasks:
            await asyncio.gather(*list(self.proxy_tasks), return_exceptions=True)
        if self.server.handlers:
            await asyncio.gather(*list(self.server.handlers), return_exceptions=True)
        self.server.running = False
        await self.loop
        self.server.backend.close()
        self.server.store.close()
        self.temporary.cleanup()

    async def forward(self, reader, writer):
        task = asyncio.current_task()
        self.proxy_tasks.add(task)
        self.writers.add(writer)
        upstream = None
        pumps = []
        try:
            if self.block_connections:
                return
            source, upstream = await asyncio.open_connection("127.0.0.1", self.port)
            self.writers.add(upstream)

            async def pump(incoming, outgoing, from_server):
                while True:
                    head = await incoming.readexactly(wire.HEADER.size)
                    kind, size = wire.unpack_header(head)
                    body = await incoming.readexactly(size)
                    if not from_server and kind == wire.SNAPSHOT and not body:
                        self.repairs.append(time.monotonic())
                    if from_server and kind == wire.RECEIPT and self.drop_receipt:
                        self.drop_receipt = False
                        return
                    if from_server and kind == wire.SNAPSHOT and self.corrupt_snapshot:
                        self.corrupt_snapshot = False
                        damaged = bytearray(body)
                        damaged[wire.SNAP.size + 32] ^= 1
                        body = bytes(damaged)
                    outgoing.write(head + body)
                    await outgoing.drain()

            pumps = [asyncio.create_task(pump(reader, upstream, False)),
                     asyncio.create_task(pump(source, writer, True))]
            await asyncio.wait(pumps, return_when=asyncio.FIRST_COMPLETED)
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            for pump in pumps:
                pump.cancel()
            if pumps:
                await asyncio.gather(*pumps, return_exceptions=True)
            for connection in (writer, upstream):
                if connection:
                    connection.close()
                    try:
                        await connection.wait_closed()
                    except ConnectionError:
                        pass
                    self.writers.discard(connection)
            self.proxy_tasks.discard(task)

    async def client(self, script, name="normal"):
        replay, report = self.work / (name + ".script"), self.work / (name + ".json")
        replay.write_text(script)
        process = await asyncio.create_subprocess_exec(
            os.environ.get("ANAPHORUM_TEST_CLIENT", str(ROOT / "build/cascade")), "--headless", "--server", "127.0.0.1",
            "--port", str(self.proxy_port), "--identity", str(self.work / "device.keys"),
            "--network-report", str(report), "--script", str(replay),
            env={**os.environ, "SDL_VIDEODRIVER": "dummy"}, cwd=self.work,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        return process, report

    async def charge_enrolled_player(self):
        for _ in range(200):
            if self.server.world.active:
                # Unit-test scheduling only. Shared rules still produce the
                # personal budget; no client-supplied state or fixture ID.
                for _ in range(2300):
                    self.server.world.tick()
                return
            await asyncio.sleep(.01)
        self.fail("normal client did not enrol")

    async def finish(self, process, report):
        out, error = await asyncio.wait_for(process.communicate(), 12)
        self.assertEqual(process.returncode, 0, (out.decode(), error.decode()))
        return json.loads(report.read_text())

    async def test_lost_receipt_reconnects_without_repeating_mutation(self):
        self.drop_receipt = True
        process, path = await self.client("idle 1000\ncondense 1\nidle 2500\nquit\n")
        await self.charge_enrolled_player()
        report = await self.finish(process, path)
        self.assertGreaterEqual(report["connections"], 2)
        self.assertGreaterEqual(report["successful_actions"], 1)
        self.assertEqual(self.server.store.db.execute("SELECT count(*) FROM events").fetchone()[0], 1)
        history = (self.work / "device.keys.history").read_bytes()
        self.assertEqual(history, b"ANJ1" + struct.pack("<I", 0))

    async def test_bad_snapshot_crc_requests_bounded_full_repair(self):
        self.corrupt_snapshot = True
        start = time.monotonic()
        process, path = await self.client("idle 1500\nquit\n")
        report = await self.finish(process, path)
        self.assertGreater(report["snapshots"], 5)
        self.assertTrue(self.repairs)
        self.assertLess(self.repairs[0] - start, .9)  # before periodic heartbeat

    async def test_offline_journal_survives_process_restart_and_reconciles(self):
        process, path = await self.client("idle 1200\ncondense 1\nidle 500\nsave\nquit\n")
        await self.charge_enrolled_player()
        await asyncio.sleep(.3)  # allow authoritative budget snapshot to arrive
        self.block_connections = True
        for writer in list(self.writers):
            writer.close()
        await self.finish(process, path)
        history = (self.work / "device.keys.history").read_bytes()
        self.assertEqual(struct.unpack("<I", history[4:8])[0], 1)
        self.assertEqual(history[8], wire.OFFLINE)
        self.assertEqual((self.work / "device.keys.cache").stat().st_mode & 0o077, 0)
        self.assertEqual(self.server.store.db.execute("SELECT count(*) FROM events").fetchone()[0], 0)
        self.block_connections = False
        process, path = await self.client("idle 1500\nquit\n", "restart")
        report = await self.finish(process, path)
        self.assertGreaterEqual(report["successful_actions"], 1)
        self.assertEqual(self.server.store.db.execute("SELECT count(*) FROM events").fetchone()[0], 1)
        self.assertEqual((self.work / "device.keys.history").read_bytes(), b"ANJ1" + struct.pack("<I", 0))


if __name__ == "__main__":
    unittest.main()
