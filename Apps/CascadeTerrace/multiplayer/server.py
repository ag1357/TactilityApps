"""Single-process Anaphorum development authority.

Plaintext credential transport is explicitly development-only, loopback by
default. Public TLS/key provisioning is a later deployment gate, not tailnet
membership baked into account semantics.
"""
import argparse
import asyncio
import ipaddress
import json
import pathlib
import resource
import signal
import struct
import time

from . import protocol as wire
from .backend import LegacyBackend
from .inference import broker
from .fingerprint import fingerprint
from .simulation import World
from .storage import Store


def percentile(samples, p):
    return sorted(samples)[min(len(samples) - 1, int((len(samples) - 1) * p))] if samples else 0


class Server:
    def __init__(self, config):
        self.config = config
        self.store = Store(config.database, config.seed)
        self.backend = LegacyBackend(config.seed)
        self.world = World(self.backend, self.store, config.interest_radius, config.peer_limit)
        self.broker = broker(config.inference_endpoint)
        self.connections = {}
        self.handlers = set()
        self.sent = self.received = self.max_players = 0
        self.started = time.monotonic()
        self.loop_us = []
        self.running = True
        self.content_hash = fingerprint()

    async def packet(self, reader):
        header = await asyncio.wait_for(reader.readexactly(wire.HEADER.size), self.config.idle_timeout)
        kind, length = wire.unpack_header(header)
        payload = await asyncio.wait_for(reader.readexactly(length), self.config.idle_timeout)
        self.received += len(header) + length
        return kind, payload

    async def send(self, writer, kind, payload):
        message = wire.frame(kind, payload)
        writer.write(message)
        await asyncio.wait_for(writer.drain(), 2)
        self.sent += len(message)

    async def connect(self, reader, writer):
        task = asyncio.current_task()
        self.handlers.add(task)
        player = session = None
        try:
            kind, payload = await self.packet(reader)
            if kind != wire.HELLO or len(payload) != wire.AUTH.size:
                raise ValueError("hello required")
            scheme, device, credential = wire.AUTH.unpack(payload)
            if scheme != 1:
                raise ValueError("authentication scheme")
            fresh = device == bytes(16)
            if fresh:
                if not self.config.dev_enrolment or credential != bytes(32):
                    raise ValueError("development enrolment disabled")
                account, player, device, credential = self.store.enrol()
            account, player, session = self.store.login(device, credential, int(time.time()), self.config.session_ttl)
            self.world.join(player, session)
            self.connections[player] = writer
            self.max_players = max(self.max_players, len(self.connections))
            # IDs are typed, independent random values. Only fresh enrolment
            # receives its secret; never replicate another account/device.
            welcome = (account + player + device + session + self.store.world_id +
                       self.content_hash + (credential if fresh else bytes(32)) +
                       struct.pack("<IIQ", self.config.seed, 1, self.store.device_sequence(device)) +
                       self.world.scope_id + b"".join(struct.pack("<I16s", role, entity)
                           for entity, role in self.world.targets.items()))
            await self.send(writer, wire.WELCOME, welcome)
            await self.send(writer, wire.SNAPSHOT, self.world.snapshot(player))
            deadline = time.monotonic() + self.config.session_ttl
            while self.running and time.monotonic() < deadline:
                kind, payload = await self.packet(reader)
                if time.monotonic() >= deadline:
                    break
                if kind == wire.INPUT and len(payload) == wire.STEP.size:
                    self.world.input(player, *wire.STEP.unpack(payload))
                elif kind in (wire.ACTION, wire.OFFLINE) and len(payload) == wire.OP.size:
                    reply = self.world.action(player, device, payload, kind == wire.OFFLINE)
                    await self.send(writer, wire.RECEIPT, reply)
                elif kind == wire.SNAPSHOT and not payload:
                    await self.send(writer, wire.SNAPSHOT, self.world.snapshot(player))
                else:
                    raise ValueError("message shape")
        except (ValueError, asyncio.IncompleteReadError, ConnectionError, TimeoutError):
            pass
        finally:
            if player and self.world.active.get(player) == session:
                self.connections.pop(player, None)
                self.world.leave(player, session)
            writer.close()
            try:
                await writer.wait_closed()
            except ConnectionError:
                pass
            self.handlers.discard(task)

    async def loop(self):
        replicate = checkpoint = time.monotonic()
        while self.running:
            start = time.monotonic()
            self.world.tick()
            if start - replicate >= self.config.replication_ms / 1000:
                replicate = start
                # Backpressure: never queue an unbounded snapshot backlog.
                for player, writer in list(self.connections.items()):
                    if writer.is_closing():
                        continue
                    if writer.transport.get_write_buffer_size() > wire.MAX_PACKET * 2:
                        writer.close()
                        continue
                    message = wire.frame(wire.SNAPSHOT, self.world.snapshot(player))
                    writer.write(message)
                    self.sent += len(message)
            if start - checkpoint >= self.config.checkpoint_seconds:
                self.store.checkpoint(((p, self.backend.state(p)) for p in self.world.active), self.backend.state())
                checkpoint = start
            # Don't run catch-up bursts after a slow tick. Record actual tick
            # work separately from the configured cadence.
            self.loop_us.append((time.monotonic() - start) * 1000000)
            if len(self.loop_us) > 10000:
                self.loop_us.pop(0)
            await asyncio.sleep(max(0.001, .02 - (time.monotonic() - start)))

    def metrics(self):
        elapsed = time.monotonic() - self.started
        usage = resource.getrusage(resource.RUSAGE_SELF)
        return {
            "players_peak": self.max_players, "elapsed_s": elapsed,
            "bytes_sent": self.sent, "bytes_received": self.received,
            "bytes_client_second": (self.sent + self.received) / max(1, self.max_players) / max(.001, elapsed),
            "world_tick_us": {k: percentile(self.world.tick_us, p) for k, p in
                              [("p50", .5), ("p95", .95), ("p99", .99)]},
            "authority_loop_work_us": {k: percentile(self.loop_us, p) for k, p in
                                       [("p50", .5), ("p95", .95), ("p99", .99)]},
            "ticks": self.world.ticks,
            "cpu_seconds": usage.ru_utime + usage.ru_stime, "rss_max_kib": usage.ru_maxrss,
            "persistence_us": {"p50": percentile(self.world.persistence_us, .5), "p99": percentile(self.world.persistence_us, .99)},
            "reconciliation_us": {"p50": percentile(self.world.reconciliation_us, .5), "p99": percentile(self.world.reconciliation_us, .99)},
            "interest_fanout_max": self.world.max_fanout,
            "interest_fanout_sum": self.world.fanout,
            "inference_demand": self.broker.demand, "offline_history_demand": self.world.offline_demand,
            "scope": "Development transport; no public deployment or native P4 claim",
        }

    async def run(self):
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGTERM, signal.SIGINT):
            loop.add_signal_handler(sig, stop.set)
        listener = await asyncio.start_server(self.connect, self.config.bind, self.config.port, limit=wire.MAX_PACKET)
        print(json.dumps({"ready": True, "address": self.config.bind,
                          "port": listener.sockets[0].getsockname()[1]}), flush=True)
        task = asyncio.create_task(self.loop())
        await stop.wait()
        listener.close()
        await listener.wait_closed()
        self.running = False
        await task
        for writer in list(self.connections.values()):
            writer.close()
        if self.handlers:
            await asyncio.gather(*list(self.handlers), return_exceptions=True)
        self.store.checkpoint(((p, self.backend.state(p)) for p in self.world.active), self.backend.state())
        self.store.close()
        self.backend.close()
        result = self.metrics()
        if self.config.metrics:
            pathlib.Path(self.config.metrics).write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7788)
    parser.add_argument("--database", required=True)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--dev-enrolment", action="store_true")
    parser.add_argument("--allow-plaintext-lan", action="store_true")
    parser.add_argument("--interest-radius", type=int, default=30000)
    parser.add_argument("--peer-limit", type=int, default=32)
    parser.add_argument("--replication-ms", type=int, default=100)
    parser.add_argument("--checkpoint-seconds", type=float, default=30)
    parser.add_argument("--inference-endpoint", default="mock")
    parser.add_argument("--session-ttl", type=int, default=3600)
    parser.add_argument("--idle-timeout", type=int, default=15)
    parser.add_argument("--metrics")
    config = parser.parse_args()
    if not ipaddress.ip_address(config.bind).is_loopback and not config.allow_plaintext_lan:
        parser.error("non-loopback development transport requires --allow-plaintext-lan; not public-ready")
    if not 1 <= config.peer_limit <= 32 or config.replication_ms < 20 or config.interest_radius <= 0:
        parser.error("bounded replica configuration")
    if config.checkpoint_seconds <= 0 or config.session_ttl <= 0 or config.idle_timeout <= 0:
        parser.error("positive persistence/session interval")
    if not 0 <= config.seed <= 0xffffffff or not 0 <= config.port <= 65535:
        parser.error("seed/port range")
    asyncio.run(Server(config).run())


if __name__ == "__main__":
    main()
