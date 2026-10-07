"""Authority/protocol/storage tests, not fixture-ID runtime accommodations."""
import asyncio
import hashlib
import pathlib
import struct
import tempfile
import unittest

from multiplayer import protocol as wire
from multiplayer.backend import LegacyBackend
from multiplayer.inference import InferenceRequest, MockBroker
from multiplayer.simulation import World
from multiplayer.storage import Store


class Authority(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = pathlib.Path(self.tmp.name) / "world.sqlite"
        self.store = Store(self.path, 42)
        self.backend = LegacyBackend(42)
        self.world = World(self.backend, self.store)
        self.account, self.player, self.device, self.secret = self.store.enrol()
        _, _, self.session = self.store.login(self.device, self.secret, 1, 100)
        self.world.join(self.player, self.session)

    def tearDown(self):
        self.backend.close()
        self.store.close()
        self.tmp.cleanup()

    def op(self, sequence, opcode=2, base=None, amount=1, role=0, scope=None):
        target = next(k for k, v in self.world.targets.items() if v == role)
        return wire.OP.pack(sequence, self.world.revision if base is None else base, 1,
                            opcode, 0, amount, target, scope or self.world.scope_id)

    def test_authentication_and_namespace(self):
        self.assertEqual(len({self.account, self.player, self.device, self.session}), 4)
        with self.assertRaises(ValueError):
            self.store.login(self.device, bytes(32), 1, 100)
        self.assertEqual(self.store.login(self.device, self.secret, 2, 100)[1], self.player)

    def test_input_authority_sequence_and_range(self):
        before = self.backend.pose(self.player)
        with self.assertRaises(ValueError):
            self.world.input(self.player, 2, 0, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            self.world.input(self.player, 1, 1001, 0, 0, 0, 0)
        self.world.input(self.player, 1, 1000, 0, 0, 0, 1)
        self.assertEqual(before, self.backend.pose(self.player))
        self.world.tick()
        self.assertEqual(self.world.ack[self.player], 1)
        self.assertNotEqual(before, self.backend.pose(self.player))
        for seq in range(2, 66):
            self.world.input(self.player, seq, 0, 0, 0, 0, 0)
        with self.assertRaises(ValueError):
            self.world.input(self.player, 66, 0, 0, 0, 0, 0)

    def charge(self):
        for _ in range(2300):
            self.world.tick()

    def test_durable_idempotent_action_and_changed_retry(self):
        self.charge()
        request = self.op(1)
        result = self.world.action(self.player, self.device, request)
        self.assertEqual(struct.unpack("<QII", result)[1], 0)
        state = self.backend.state(self.player)
        self.assertEqual(self.world.action(self.player, self.device, request), result)
        self.assertEqual(self.backend.state(self.player), state)
        with self.assertRaises(ValueError):
            self.world.action(self.player, self.device, self.op(1, amount=2))
        self.assertEqual(self.world.revision, 1)

    def test_commit_failure_rolls_back(self):
        self.charge()
        world, state = self.backend.state(), self.backend.state(self.player)
        original = self.store.commit
        self.store.commit = lambda *args: (_ for _ in ()).throw(OSError("disk"))
        with self.assertRaises(OSError):
            self.world.action(self.player, self.device, self.op(1))
        self.store.commit = original
        self.assertEqual(self.backend.state(), world)
        self.assertEqual(self.backend.state(self.player), state)
        self.assertEqual(self.world.revision, 0)

    def test_offline_history_reconciles_only_authoritative_personal_budget(self):
        self.charge()
        self.world.action(self.player, self.device, self.op(1))
        # Second operation's stale base is allowed only for personal spending,
        # and fails because the actual authority budget was already spent.
        receipt = self.world.action(self.player, self.device, self.op(2, base=0), offline=True)
        self.assertEqual(struct.unpack("<QII", receipt)[1], 1)
        receipt = self.world.action(self.player, self.device, self.op(3, opcode=4), offline=True)
        self.assertEqual(struct.unpack("<QII", receipt)[1], 4)
        with self.assertRaises(ValueError):
            self.world.action(self.player, self.device, self.op(4, scope=bytes(16)))

    def test_backup_migration_world_and_receipts(self):
        self.charge()
        request = self.op(1)
        receipt = self.world.action(self.player, self.device, request)
        state = self.backend.state(self.player)
        self.store.checkpoint([(self.player, state)], self.backend.state())
        target = pathlib.Path(self.tmp.name) / "migration.sqlite"
        self.store.backup(target)
        moved = Store(target, 42)
        self.assertEqual(moved.world_id, self.store.world_id)
        self.assertEqual(moved.receipt(self.device, 1, request), (0, 1))
        new_backend = LegacyBackend(42)
        new_world = World(new_backend, moved)
        new_world.join(self.player, self.session)
        self.assertEqual(new_backend.state(self.player), state)
        self.assertEqual(new_world.action(self.player, self.device, request), receipt)
        new_backend.close()
        moved.close()

    def test_no_total_population_match_cap_and_interest_bound(self):
        # Generic interest test backend, not a claim of C-world load speed.
        class Spatial:
            def __init__(self):
                self.positions = {}
            def pose(self, player):
                return self.positions[player]
        spatial = Spatial()
        for i in range(1000):
            identity = i.to_bytes(16, "little")
            spatial.positions[identity] = (i * 1000, 0, 0, 0, 0, 1, 0, 0)
        original = self.world.backend
        self.world.backend = spatial
        original_poses = self.world.poses
        self.world.poses = spatial.positions
        self.world.grid = {}
        for player, pose in spatial.positions.items():
            self.world.grid.setdefault((pose[0] // self.world.cell_size, 0), set()).add(player)
        peers = self.world.peers(bytes(16))
        self.assertTrue(0 < len(peers) <= 32)
        self.assertTrue(all(distance <= 30000 ** 2 for distance, _, _ in peers))
        self.assertFalse(any(peer == (999).to_bytes(16, "little") for _, peer, _ in peers))
        self.world.backend = original
        self.world.poses = original_poses

    def test_protocol_bounds_and_mock_broker(self):
        with self.assertRaises(ValueError):
            wire.frame(wire.INPUT, bytes(wire.MAX_PACKET + 1))
        with self.assertRaises(ValueError):
            wire.unpack_header(wire.HEADER.pack(b"ANP1", 99, wire.INPUT, 0))
        broker = MockBroker()
        result = asyncio.run(broker.propose(InferenceRequest(self.player, bytes(16), (), "hello")))
        self.assertEqual(result["actions"], ())
        self.assertEqual(broker.demand, 1)

    def test_dense_query_cache_excludes_each_caller_and_invalidates(self):
        original = self.world.poses[self.player]
        for i in range(100):
            identity = i.to_bytes(16, "big")
            self.world.poses[identity] = original
            self.world.grid[(original[0] // self.world.cell_size,
                             original[2] // self.world.cell_size)].add(identity)
        first = self.world.peers(self.player)
        second = self.world.peers(bytes(16))
        self.assertEqual(len(first), 32)
        self.assertEqual(len(second), 32)
        self.assertFalse(any(peer == bytes(16) for _, peer, _ in second))
        self.assertEqual(len(self.world.nearby_cache), 1)
        _, other, device, secret = self.store.enrol()
        _, _, session = self.store.login(device, secret, 2, 100)
        self.world.join(other, session)
        self.assertEqual(self.world.nearby_cache, {})

    def test_archive_permission_and_schema_fail_closed(self):
        self.assertEqual(self.path.stat().st_mode & 0o077, 0)
        target = pathlib.Path(self.tmp.name) / "schema.sqlite"
        self.store.backup(target)
        moved = Store(target, 42)
        with moved.db:
            moved.db.execute("UPDATE meta SET value=? WHERE key='schema'", (b"unknown",))
        moved.close()
        with self.assertRaises(ValueError):
            Store(target, 42)

    def test_leave_storage_failure_still_releases_projection(self):
        checkpoint = self.store.checkpoint
        self.store.checkpoint = lambda *args: (_ for _ in ()).throw(OSError("injected"))
        with self.assertRaises(OSError):
            self.world.leave(self.player, self.session)
        self.assertNotIn(self.player, self.world.active)
        self.assertNotIn(self.player, self.backend.players)
        self.store.checkpoint = checkpoint


if __name__ == "__main__":
    unittest.main()
