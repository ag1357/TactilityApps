"""Content adapter into the normal game's C authority and collision code."""
import ctypes as C
import hashlib
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


class LegacyBackend:
    item_count = 8
    offline_operations = frozenset((2,))  # content CONDENSE

    @staticmethod
    def scope_identity(world):
        return hashlib.sha256(world + b"exterior").digest()[:16]

    @staticmethod
    def entity_bindings(world):
        return {hashlib.sha256(world + b"cascade-role:" + str(role).encode()).digest()[:16]: role
                for role in (0, 1, 3, 1100, 1101, 1200)}

    def __init__(self, seed):
        self.lib = C.CDLL(str(ROOT / "build/libanaphorum.so"))
        signatures = {
            "oa_world": (C.c_void_p, [C.c_uint32]),
            "oa_player": (C.c_void_p, [C.c_void_p]),
            "oa_free": (None, [C.c_void_p]),
            "oa_step": (None, [C.c_void_p, C.c_void_p] + [C.c_int] * 5),
            "oa_clock": (None, [C.c_void_p, C.c_uint]),
            "oa_personal_clock": (None, [C.c_void_p, C.c_void_p, C.c_uint]),
            "oa_pose": (None, [C.c_void_p, C.POINTER(C.c_int32)]),
            "oa_action": (C.c_int, [C.c_void_p, C.c_void_p, C.c_uint, C.c_uint, C.c_int, C.c_uint]),
            "oa_state": (C.c_size_t, [C.c_void_p, C.c_void_p, C.c_void_p, C.c_size_t]),
            "oa_world_state": (C.c_size_t, [C.c_void_p, C.c_void_p, C.c_size_t]),
            "oa_restore_world": (C.c_int, [C.c_void_p, C.c_void_p, C.c_size_t]),
            "oa_restore_player": (C.c_int, [C.c_void_p, C.c_void_p, C.c_size_t]),
        }
        for name, (result, args) in signatures.items():
            f = getattr(self.lib, name)
            f.restype, f.argtypes = result, args
        self.world = self.lib.oa_world(seed)
        if not self.world:
            raise MemoryError("world")
        self.players = {}
        self.buffer = C.create_string_buffer(32768)

    def join(self, player, saved=None):
        if player not in self.players:
            p = self.lib.oa_player(self.world)
            if not p:
                raise MemoryError("player projection")
            if saved and not self.lib.oa_restore_player(p, saved, len(saved)):
                self.lib.oa_free(p)
                raise ValueError("player checkpoint")
            self.players[player] = p

    def pose(self, player):
        out = (C.c_int32 * 8)()
        self.lib.oa_pose(self.players[player], out)
        return tuple(out)

    def step(self, player, sample):
        self.lib.oa_step(self.world, self.players[player], *sample)

    def clock(self, ms, active):
        self.lib.oa_clock(self.world, ms)
        for p in active:
            self.lib.oa_personal_clock(self.world, self.players[p], ms)

    def action(self, player, op, item, amount, target):
        return bool(self.lib.oa_action(self.world, self.players[player], op, item, amount, target))

    def state(self, player=None):
        n = (self.lib.oa_world_state(self.world, self.buffer, len(self.buffer)) if player is None
             else self.lib.oa_state(self.world, self.players[player], self.buffer, len(self.buffer)))
        if not n:
            raise ValueError("checkpoint capacity")
        return self.buffer.raw[:n]

    def restore(self, state):
        if not self.lib.oa_restore_world(self.world, state, len(state)):
            raise ValueError("world checkpoint")

    def restore_player(self, player, state):
        if not self.lib.oa_restore_player(self.players[player], state, len(state)):
            raise ValueError("player checkpoint")

    def drop(self, player):
        self.lib.oa_free(self.players.pop(player))

    def close(self):
        for p in self.players.values():
            self.lib.oa_free(p)
        self.players.clear()
        self.lib.oa_free(self.world)
