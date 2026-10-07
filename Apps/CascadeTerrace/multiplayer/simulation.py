"""One logical world. Authenticated messages enter here, never sockets.
Transient inputs/presentation and durable consequential events are distinct.
"""
from collections import defaultdict, deque
import hashlib
import heapq
import struct
import time
import zlib

from . import protocol as wire


class World:
    def __init__(self, backend, store, interest_radius=30000, peer_limit=32, cell_size=16000):
        self.backend, self.store = backend, store
        self.radius, self.peer_limit, self.cell_size = interest_radius, peer_limit, cell_size
        self.active, self.inputs, self.ack, self.enqueued = {}, {}, {}, {}
        self.revision = 0
        self.ticks = self.fanout = self.max_fanout = 0
        self.tick_us, self.persistence_us, self.reconciliation_us = deque(maxlen=10000), deque(maxlen=10000), deque(maxlen=10000)
        self.offline_demand = self.inference_demand = 0
        last = store.latest()
        checkpoint = store.world_checkpoint()
        if checkpoint and (not last or checkpoint[0] >= last[0]):
            last = checkpoint
        if last:
            self.revision = last[0]
            backend.restore(last[1])
        self.world_hash = hashlib.sha256(backend.state()).digest()
        self.poses = {}
        self.grid = defaultdict(set)
        self.nearby_cache = {}
        self.scope_id = backend.scope_identity(store.world_id)
        # Content-local roles terminate at this adapter. The wire carries
        # stable typed entity/scope IDs, not legacy array indexes.
        self.targets = backend.entity_bindings(store.world_id)
        self.clock_ms = 0

    def join(self, player, session):
        if player in self.active:
            raise ValueError("player already online")
        saved = self.store.player(player)
        self.backend.join(player, saved[0] if saved else None)
        self.active[player] = session
        self.inputs[player], self.ack[player], self.enqueued[player] = deque(), 0, 0
        self.poses[player] = pose = self.backend.pose(player)
        self.grid[(pose[0] // self.cell_size, pose[2] // self.cell_size)].add(player)
        self.nearby_cache.clear()

    def leave(self, player, session):
        if self.active.get(player) != session:
            return
        try:
            self.store.checkpoint([(player, self.backend.state(player))])
        finally:
            self.backend.drop(player)
            self.poses.pop(player, None)
            self.nearby_cache.clear()
            del self.active[player], self.inputs[player], self.ack[player], self.enqueued[player]

    def input(self, player, sequence, forward, strafe, yaw, jump, run):
        if player not in self.active or sequence != self.enqueued[player] + 1:
            raise ValueError("input sequence")
        if abs(forward) > 1000 or abs(strafe) > 1000 or not 0 <= yaw < 360 or not 0 <= jump <= 1 or not 0 <= run <= 1:
            raise ValueError("input range")
        queue = self.inputs[player]
        if len(queue) >= 64:
            raise ValueError("input backlog")
        queue.append((sequence, forward, strafe, yaw, jump, run))
        self.enqueued[player] = sequence

    def tick(self, ms=20):
        begin = time.perf_counter_ns()
        self.clock_ms += ms
        self.backend.clock(ms, self.active)
        self.grid = defaultdict(set)
        self.nearby_cache.clear()
        for player in self.active:
            queue = self.inputs[player]
            if queue:
                sample = queue.popleft()
                self.backend.step(player, sample[1:])
                self.ack[player] = sample[0]
            else:
                self.backend.step(player, (0, 0, self.backend.pose(player)[3], 0, 0))
            pose = self.backend.pose(player)
            self.poses[player] = pose
            self.grid[(pose[0] // self.cell_size, pose[2] // self.cell_size)].add(player)
        self.ticks += 1
        self.world_hash = hashlib.sha256(self.backend.state()).digest()
        self.tick_us.append((time.perf_counter_ns() - begin) / 1000)

    def peers(self, player):
        at = self.poses[player]
        # Identical public spatial queries share work within this tick.
        # Include one extra result so any caller can remove itself. The key
        # must grow with authorization policy if visibility becomes private.
        key = (self.scope_id, *at[:3])
        if key not in self.nearby_cache:
            cells = (self.radius + self.cell_size - 1) // self.cell_size
            candidates = set()
            for x in range(at[0] // self.cell_size - cells, at[0] // self.cell_size + cells + 1):
                for z in range(at[2] // self.cell_size - cells, at[2] // self.cell_size + cells + 1):
                    candidates.update(self.grid.get((x, z), ()))
            nearby = []
            for peer in candidates:
                pos = self.poses.get(peer)
                if pos is None:
                    continue
                distance = sum((pos[i] - at[i]) ** 2 for i in range(3))
                if distance <= self.radius ** 2:
                    nearby.append((distance, peer, pos))
            self.nearby_cache[key] = heapq.nsmallest(self.peer_limit + 1, nearby)
        selected = [candidate for candidate in self.nearby_cache[key] if candidate[1] != player][:self.peer_limit]
        self.fanout += len(selected)
        self.max_fanout = max(self.max_fanout, len(selected))
        return selected

    def snapshot(self, player):
        state = self.backend.state(player)
        peers = self.peers(player)
        return (wire.SNAP.pack(self.ack[player], self.revision, self.clock_ms, self.scope_id, len(state), len(peers), zlib.crc32(state)) +
                self.world_hash + state +
                b"".join(wire.PEER.pack(peer, self.scope_id, *pose) for _, peer, pose in peers))

    def action(self, player, device, request, offline=False):
        begin = time.perf_counter_ns()
        sequence, base, epoch, op, item, amount, entity, scope = wire.OP.unpack(request)
        if scope != self.scope_id or entity not in self.targets:
            raise ValueError("entity/scope identity")
        target = self.targets[entity]
        if not 0 < sequence < 2 ** 63 or amount < 0 or amount > 1000000 or item >= self.backend.item_count:
            raise ValueError("action range")
        previous = self.store.receipt(device, sequence, request)
        if previous:
            return struct.pack("<QII", sequence, *previous)
        if sequence <= self.store.device_sequence(device):
            raise ValueError("nonmonotone action sequence")
        prior_world, prior_player = self.backend.state(), self.backend.state(player)
        # Stale-world offline operations are never accepted by replaying a
        # client-provided state. Initial safe vocabulary is personal CONDENSE,
        # which spends only already-authoritative personal energy.
        if offline and op not in self.backend.offline_operations:
            status = 4
        elif (base > self.revision if offline else base != self.revision) or epoch != 1:
            status = 2
        else:
            status = 0 if self.backend.action(player, op, item, amount, target) else 1
        self.revision += 1
        world, state = self.backend.state(), self.backend.state(player)
        try:
            self.store.commit(self.revision, player, device, sequence, request, status, world, state,
                              "offline" if offline else "online")
        except Exception:
            self.revision -= 1
            self.backend.restore(prior_world)
            self.backend.restore_player(player, prior_player)
            raise
        self.world_hash = hashlib.sha256(world).digest()
        self.nearby_cache.clear()
        self.persistence_us.append((time.perf_counter_ns() - begin) / 1000)
        if offline:
            self.offline_demand += 1
            self.reconciliation_us.append((time.perf_counter_ns() - begin) / 1000)
        return struct.pack("<QII", sequence, status, self.revision)
