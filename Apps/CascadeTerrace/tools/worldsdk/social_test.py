#!/usr/bin/env python3
"""Gate 6 test (canonical events, witnesses and social projections).
Proves, against the C engine through libsdk.so over the committed Terrace
Commons fixture, with an independent Python mirror of every rule:
  - witness projection: a committed repair informs exactly the witnesses
    the shared gate admits (kyra 940, dax/marisol 790 by the mirrored
    (30000 - manhattan) // 30 arithmetic) and leaves the out-of-range and
    the occluded observer untouched;
  - formation parity: every authority action (repair, show, tell, report,
    exchange, inform, file, settle) produces the same evidence table in C
    and in the Python mirror, field for field, including
    (observer, root, teller, kind) replacement semantics;
  - projection parity: ws_social_view and ws_social_cite equal the pure
    Python mirrors for every observer x subject pair, at every scenario
    step and across decay clocks (query-frequency invariance, pinned
    survival, minor history gone past the window);
  - institutional delivery parity: pending dispatches deliver at the
    mirrored delay with degraded 3/4 confidence capped at 750, replace
    instead of stack, and publish FACTION_NOTICE feed entries;
  - retry parity: an exact replay of a committed command replays its
    receipt with no repeated cost, a different op under a consumed
    sequence is stale, in both languages;
  - fail-closed parity: the same malformed informs/files are refused with
    the same error codes and leave the state untouched;
  - wire v4 parity: the Python mirror of the whole state codec emits
    byte-identical wire (header, sections, CRC), the mirrored payload CRC
    is the state hash, and truncated or corrupt wires fail closed;
  - generalization: the same proofs over the committed Cascade product.
Standard library only."""

import ctypes as C
import json
import pathlib
import struct
import sys
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from qualify import Pos, Recipe, ROOT
from server import ACTIONS, Address, Context, Operation, State, View, library
from sdk import (
    ACTION_VALENCE,
    CTX_DISPATCH,
    CTX_NONE,
    CTX_RESTITUTION,
    CTX_SHOWN,
    EV_ACT,
    EV_CAP,
    EV_CLAIM,
    EV_CONTRADICT,
    EV_PLAYER,
    EV_PINNED,
    EV_REPORT,
    EV_REPORTED_MAX,
    evidence_put,
    social_cite,
    social_settle,
    social_view,
    witness_confidence,
)

OK, FORMAT, VERSION, BOUNDS, DUPLICATE, REFERENCE, DISCONNECTED, DENIED, STALE, FULL = range(10)
SHOW, TELL, REPORT, EXCHANGE, REPAIR, DAMAGE = (
    ACTIONS["SHOW"], ACTIONS["TELL"], ACTIONS["REPORT"], ACTIONS["EXCHANGE"],
    ACTIONS["REPAIR"], ACTIONS["DAMAGE"],
)
# Fixture entity indexes (module order): station 4, market 6, kyra 15,
# hydro_field 16, dax 17, marisol 18, oren 19, clerk 20, toma 21.
STATION, MARKET, KYRA, FIELD, DAX, MARISOL, OREN, CLERK, TOMA = 4, 6, 15, 16, 17, 18, 19, 20, 21


def h_npc(i):
    return i + 1


H_PLAYER = EV_PLAYER


class Mirror:
    """Python-side model of the canonical social state, maintained by
    mirroring the formation rules of each authority action."""

    def __init__(self, npc_indexes, players, modules):
        self.npcs = set(npc_indexes)
        self.npc_count = max(npc_indexes) + 1 if npc_indexes else 0
        self.players = [{"id": p} for p in players]
        self.modules = modules
        self.ev = []
        self.pend = []
        self.feed = []
        self.revision = 1
        self.clock_s = 0

    def sync(self, lib, s, label, checks):
        """The C social tables must equal the mirror, and every C view and
        citation must equal the mirror's pure functions."""
        checks[0] += 1
        assert s.ev_count == len(self.ev), f"{label}: ev_count {s.ev_count} != {len(self.ev)}"
        for i, rec in enumerate(self.ev):
            e = s.ev[i]
            got = (e.observer, e.subject, e.teller, e.root, e.kind, e.confidence, e.context, e.clock_s, e.salience, e.valence)
            want = (rec["observer"], rec["subject"], rec["teller"], rec["root"], rec["kind"], rec["confidence"], rec["context"], rec["clock"], rec["salience"], rec["valence"])
            assert got == want, f"{label}: record {i} {got} != {want}"
        checks[0] += 1
        assert s.pend_count == len(self.pend), f"{label}: pend_count {s.pend_count} != {len(self.pend)}"
        for i, rec in enumerate(self.pend):
            p = s.pend[i]
            got = (p.observer, p.subject, p.teller, p.root, p.deliver_s, p.kind, p.confidence, p.context, p.salience, p.valence)
            want = (rec["observer"], rec["subject"], rec["teller"], rec["root"], rec["deliver_s"], rec["kind"], rec["confidence"], rec["context"], rec["salience"], rec["valence"])
            assert got == want, f"{label}: pending {i}"
        checks[0] += 1
        assert s.revision == self.revision, f"{label}: revision {s.revision} != {self.revision}"
        # Delivery feed parity: the faction notices the mirror models must
        # equal the C ones (world-event feed entries are act history, not
        # social delivery; the C gate proves those).
        faction = [f for f in s.feed[: s.feed_count] if f.kind == 5]
        checks[0] += 1
        assert len(faction) == len(self.feed), f"{label}: faction notices {len(faction)} != {len(self.feed)}"
        for f, rec in zip(faction, self.feed):
            checks[0] += 1
            assert (f.revision, f.actor, f.target, f.kind) == (rec["revision"], rec["actor"], rec["target"], rec["kind"]), f"{label}: faction notice {f.revision}"
        v = View()
        subjects = list(range(1, self.npc_count + 1)) + [H_PLAYER]
        for observer in subjects:
            for subject in subjects:
                pairs = [r for r in self.ev if r["observer"] == observer and r["subject"] == subject]
                got = lib.ws_social_view(C.byref(s), observer, subject, C.byref(v))
                vm = social_view(pairs, self.clock_s)
                checks[0] += 2
                assert bool(got), f"{label}: view call {observer}->{subject}"
                assert (v.trust, v.acts, v.claims, v.contradictions, v.restitutions, v.promises, v.kept, v.breaches, v.reports, v.confidence, v.watermark) == (
                    vm["trust"], vm["acts"], vm["claims"], vm["contradictions"], vm["restitutions"], vm["promises"], vm["kept"], vm["breaches"], vm["reports"], vm["confidence"], vm["watermark"]
                ), f"{label}: view {observer}->{subject} C {(v.trust, v.acts, v.claims)} != P {vm}"
                if not pairs:
                    continue
                root = C.c_uint32()
                kind = C.c_uint16()
                sal = C.c_uint16()
                got = lib.ws_social_cite(C.byref(s), observer, subject, C.byref(root), C.byref(kind), C.byref(sal))
                want = social_cite(pairs)
                checks[0] += 1
                assert bool(got) == bool(want) and (not want or (root.value, kind.value, sal.value) == want), f"{label}: cite {observer}->{subject}"

    def witness(self, event, action, occluded=()):
        """Mirror of the witness projection after a committed act: each NPC
        the shared gate admits gains direct ACT evidence at the mirrored
        confidence; everyone else stays unchanged."""
        for i in sorted(self.npcs):
            if i in occluded:
                continue
            mod = self.modules[i]
            conf = witness_confidence((mod.pos.x, mod.pos.y, mod.pos.z), event)
            if not conf:
                continue
            evidence_put(self.ev, {
                "observer": h_npc(i), "subject": H_PLAYER, "teller": 0,
                "root": self.revision, "kind": EV_ACT, "confidence": conf,
                "context": CTX_NONE, "clock": self.clock_s, "salience": 0,
                "valence": ACTION_VALENCE[action],
            })


def mirror_payload(s, with_tail):
    """Python mirror of the whole v4 state codec read from the C state.
    with_tail selects the encode form (tail section present) over the hash
    form (tail count zero, no tail section)."""
    parts = []

    def put(fmt, *vals):
        parts.append(struct.pack(fmt, *vals))

    put("<4I", *s.ancestry.word)
    put("<I", s.recipe_crc)
    put("<I", s.revision)
    put("<4H", s.count, s.player_count, s.feed_count, s.tail_count if with_tail else 0)
    for e in s.entities[: s.count]:
        put("<3I2H", e.epoch, e.revision, e.owner, e.quantity, e.health)
        put("<4H", *e.decor)
        put("<4B", e.alive, e.public_access, e.behavior, e.reserved)
    for p in s.players[: s.player_count]:
        put("<3I", p.id, p.sequence, p.credits)
        put("<7H", *p.inventory[:7])
        put("<128I", *p.completed[:128])
    for i in range(s.player_count):
        for j in range(s.player_count):
            rel = s.relations[i][j]
            put("<6h", rel.trust, rel.reliability, rel.cooperation, rel.aggression, rel.confidence, rel.promise)
    for f in s.feed[: s.feed_count]:
        put("<3I2H", f.revision, f.actor, f.epoch, f.target, f.kind)
    if with_tail:
        for o in s.tail[: s.tail_count]:
            put("<3I4H", o.sequence, o.epoch, o.base_revision, o.action, o.target, o.amount, o.aux)
    # wire 4 always carries the resource and creature sections (zero counts
    # where unused) plus the social section.
    put("<2H", s.reservoir_count, s.site_count)
    put("<I", s.clock_s)
    put(f"<{s.reservoir_count}I", *s.level[: s.reservoir_count])
    put(f"<{s.player_count}H", *s.material[: s.player_count])
    put(f"<{s.player_count}H", *s.shards[: s.player_count])
    for t in s.sites[: s.site_count]:
        put("<3i4HI", t.pos.x, t.pos.y, t.pos.z, t.reservoir, t.kind, t.extent, t.reserved, t.amount)
    put("<6I", s.recovered_total & 0xFFFFFFFF, s.recovered_total >> 32, s.used_total & 0xFFFFFFFF, s.used_total >> 32, s.lost_total & 0xFFFFFFFF, s.lost_total >> 32)
    put("<2H", s.creature_count, 0)
    for e in s.crex[: s.creature_count]:
        put("<4I4H", *e.id.word, e.kind, e.aux, e.reserved, e.pad)
    put("<2H", s.ev_count, s.pend_count)
    for e in s.ev[: s.ev_count]:
        put("<4I3HI4H", e.observer, e.subject, e.teller, e.root, e.kind, e.confidence, e.context, e.clock_s, e.salience, e.reserved, e.valence & 0xFFFF, e.pad)
    for p in s.pend[: s.pend_count]:
        put("<5I5H2H", p.observer, p.subject, p.teller, p.root, p.deliver_s, p.kind, p.confidence, p.context, p.salience, p.reserved, p.valence & 0xFFFF, p.pad)
    for rc in s.receipt[: s.player_count]:
        put("<4I6H", rc.sequence, rc.epoch, rc.base_revision, rc.revision, rc.action, rc.target, rc.amount, rc.aux, rc.status, rc.flags)
    return b"".join(parts)


def mirror_wire(s):
    payload = mirror_payload(s, True)
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    return struct.pack("<IHHII", 0x31535743, 4, 1, 16 + len(payload), crc) + payload


def main():
    lib = library()
    lib.ws_state_validate.argtypes = [C.POINTER(State), C.POINTER(Recipe)]
    lib.ws_state_validate.restype = C.c_int
    lib.ws_state_hash.argtypes = [C.POINTER(State)]
    lib.ws_state_hash.restype = C.c_uint32
    lib.ws_state_encode.argtypes = [C.POINTER(State), C.c_void_p, C.c_size_t]
    lib.ws_state_encode.restype = C.c_size_t
    lib.ws_state_decode.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_void_p, C.c_size_t]
    lib.ws_state_decode.restype = C.c_int
    lib.ws_save.argtypes = [C.POINTER(State), C.c_char_p]
    lib.ws_save.restype = C.c_int
    lib.ws_restore.argtypes = [C.POINTER(State), C.POINTER(Recipe), C.c_char_p]
    lib.ws_restore.restype = C.c_int
    results = {"stage": "social-projections", "checks": 0}
    checks = [0]

    def check(cond, label):
        checks[0] += 1
        assert cond, label

    data = (ROOT / "build/social.cws").read_bytes()
    r = Recipe()
    check(lib.ws_load(C.byref(r), data, len(data)) == OK, "fixture loads")
    src = json.loads((ROOT / "content/worlds/social.json").read_text())
    names = [m["name"] for m in src["modules"]]
    npc_indexes = [i for i, m in enumerate(r.modules[: r.count]) if m.flags & 16]
    check([names[i] for i in npc_indexes] == ["kyra", "dax", "marisol", "oren", "clerk", "toma"], "cast")

    s = State()
    lib.ws_state_init(C.byref(s), C.byref(r))
    lib.ws_join(C.byref(s), 101)
    m = Mirror(npc_indexes, [101], r.modules)
    check(s.revision == 1, "join revision")

    def op(action, target, seq, amount=0, aux=0, at=(22000, 4300, -96500), server=1, player=101, base=None):
        epoch, revision = (s.entities[target].epoch, s.entities[target].revision) if base is None else base
        return lib.ws_apply(
            C.byref(s), C.byref(r),
            Context(player, Address(Pos(*at), 0xFFFF), server),
            Operation(seq, epoch, revision, action, target, amount, aux),
        )

    def inform(observer, subject, teller, kind, conf, ctx, valence, sal, root):
        res = lib.ws_inform(C.byref(s), C.byref(r), observer, subject, teller, kind, conf, ctx, valence, sal, root)
        if res == OK:
            evidence_put(m.ev, {"observer": observer, "subject": subject, "teller": teller, "root": root, "kind": kind, "confidence": conf, "context": ctx, "clock": m.clock_s, "salience": sal, "valence": valence})
        return res

    # 1. Witnessed repair: exactly kyra (940), dax and marisol (790) gain
    # direct evidence; oren is out of range and toma is occluded by the
    # station wall, so both stay uninformed.
    s.players[0].inventory[1] = 5
    base = (s.entities[STATION].epoch, s.entities[STATION].revision)
    d = op(REPAIR, STATION, 1, 1, 0, base=base)
    check(d.status == OK and d.rewarded, "repair commits")
    m.revision = 2
    m.witness((22000, 4300, -96500), REPAIR, occluded=(TOMA,))
    m.sync(lib, s, "repair", checks)
    check(m.ev[0]["confidence"] == 940 and m.ev[1]["confidence"] == 790 and m.ev[2]["confidence"] == 790, "witness confidences")
    view = social_view([rec for rec in m.ev if rec["observer"] == h_npc(KYRA) and rec["subject"] == H_PLAYER], 0)
    check(view["trust"] == 282 and view["acts"] == 1, "kyra trust arithmetic")

    # 2. Retry parity: the exact replay returns the receipt, a different op
    # under the consumed sequence is stale, and neither moves any table.
    hash_before = lib.ws_state_hash(C.byref(s))
    d = op(REPAIR, STATION, 1, 1, 0, base=base)
    check(d.status == OK and d.rewarded and d.revision == 2, "receipt replay")
    check(lib.ws_state_hash(C.byref(s)) == hash_before, "replay changes nothing")
    d = op(REPAIR, STATION, 1, 2, 0, base=base)
    check(d.status == STALE, "sequence reuse is stale")
    check(lib.ws_state_hash(C.byref(s)) == hash_before, "stale changes nothing")
    m.sync(lib, s, "retries", checks)

    # 3. The NPC-to-NPC example (contract 6.7) as authority informs: the
    # false claim, the contradiction, the filing, the delayed delivery, the
    # degradation, the replacement (never stacking), the restitution.
    root = m.revision
    check(inform(h_npc(KYRA), h_npc(DAX), h_npc(DAX), EV_CLAIM, 500, CTX_NONE, 0, 400, root) == OK, "dax claim")
    check(inform(h_npc(KYRA), h_npc(DAX), h_npc(MARISOL), EV_CONTRADICT, 900, CTX_NONE, -300, 600, root) == OK, "marisol contradiction")
    m.sync(lib, s, "claims", checks)

    def strongest_kyra_dax():
        return max((rec for rec in m.ev if rec["observer"] == h_npc(KYRA) and rec["subject"] == h_npc(DAX)), key=lambda rec: (rec["salience"], rec["root"]))

    def file_mirror(observer, subject):
        best = strongest_kyra_dax() if subject == h_npc(DAX) else None
        return {
            "observer": observer, "subject": subject, "teller": h_npc(KYRA),
            "root": best["root"], "deliver_s": m.clock_s + 3600,
            "kind": best["kind"], "confidence": best["confidence"],
            "context": best["context"], "salience": best["salience"],
            "valence": best["valence"],
        }

    check(lib.ws_file(C.byref(s), C.byref(r), h_npc(CLERK), h_npc(KYRA), h_npc(DAX)) == OK, "file")
    m.pend.append(file_mirror(h_npc(CLERK), h_npc(DAX)))
    m.sync(lib, s, "filed", checks)
    # A tick short of the delay: nothing delivered.
    lib.ws_clock_advance(C.byref(s), C.byref(r), 3599)
    m.clock_s += 3599
    m.sync(lib, s, "3599s", checks)
    lib.ws_clock_advance(C.byref(s), C.byref(r), 1)
    m.clock_s += 1
    social_settle(vars(m))
    check(len(m.pend) == 0, "delivered in the mirror")
    m.sync(lib, s, "delivered", checks)
    delivered = [rec for rec in m.ev if rec["observer"] == h_npc(CLERK) and rec["subject"] == h_npc(DAX)]
    check(len(delivered) == 1 and delivered[0]["kind"] == EV_REPORT and delivered[0]["confidence"] == 675, "degraded 3/4 delivery")
    check(s.feed[s.feed_count - 1].kind == 5 and s.feed[s.feed_count - 1].target == CLERK, "faction notice")
    # Repeated filing of the same cause replaces: never stacks.
    check(lib.ws_file(C.byref(s), C.byref(r), h_npc(CLERK), h_npc(KYRA), h_npc(DAX)) == OK, "re-file")
    m.pend.append(file_mirror(h_npc(CLERK), h_npc(DAX)))
    lib.ws_clock_advance(C.byref(s), C.byref(r), 3600)
    m.clock_s += 3600
    social_settle(vars(m))
    m.sync(lib, s, "re-delivered", checks)
    delivered = [rec for rec in m.ev if rec["observer"] == h_npc(CLERK) and rec["subject"] == h_npc(DAX)]
    check(len(delivered) == 1 and delivered[0]["confidence"] == 675, "replacement, not stacking")
    # Restitution adds without deleting.
    check(inform(h_npc(KYRA), h_npc(DAX), 0, 4, 800, CTX_RESTITUTION, 300, 500, root) == OK, "restitution")
    m.sync(lib, s, "restitution", checks)
    view = social_view([rec for rec in m.ev if rec["observer"] == h_npc(KYRA) and rec["subject"] == h_npc(DAX)], m.clock_s)
    check(view["claims"] == 1 and view["contradictions"] == 1 and view["restitutions"] == 1 and view["trust"] == 210, "restitution arithmetic")

    # 4. Decay parity across clocks: the same buckets in both languages,
    # pinned records pinned, minor history gone past the window.
    for delta in (86400, 86400, 5 * 86400, 15 * 86400):
        lib.ws_clock_advance(C.byref(s), C.byref(r), delta)
        m.clock_s += delta
        social_settle(vars(m))
        m.sync(lib, s, f"clock +{delta}", checks)
    view = social_view([rec for rec in m.ev if rec["observer"] == h_npc(KYRA) and rec["subject"] == H_PLAYER], m.clock_s)
    check(view["trust"] == 0 and view["confidence"] == 0, "minor history decayed away")
    view = social_view([rec for rec in m.ev if rec["observer"] == h_npc(KYRA) and rec["subject"] == h_npc(DAX)], m.clock_s)
    check(view["trust"] == 210 and view["restitutions"] == 1, "pinned history survives")

    # 5. Private events and the player REPORT over the authority: show,
    # tell and exchange form evidence but no feed; a filed claim stays
    # claim-grade when delivered.
    feeds = s.feed_count
    s.players[0].inventory[2] = 1
    d = op(SHOW, KYRA, 2, 0, 2, at=(22000, 4300, -95200), server=0)
    check(d.status == OK and s.feed_count == feeds, "show is private")
    m.revision = s.revision
    m.witness((22000, 4300, -95200), SHOW, occluded=(TOMA,))
    evidence_put(m.ev, {"observer": h_npc(KYRA), "subject": H_PLAYER, "teller": H_PLAYER, "root": m.revision, "kind": EV_ACT, "confidence": 1000, "context": CTX_SHOWN, "clock": m.clock_s, "salience": 0, "valence": ACTION_VALENCE[SHOW]})
    m.sync(lib, s, "show", checks)
    d = op(TELL, KYRA, 3, 0, h_npc(DAX), at=(22000, 4300, -95200), server=0)
    check(d.status == OK and s.feed_count == feeds, "tell is private")
    m.revision = s.revision
    m.witness((22000, 4300, -95200), TELL, occluded=(TOMA,))
    evidence_put(m.ev, {"observer": h_npc(KYRA), "subject": h_npc(DAX), "teller": H_PLAYER, "root": m.revision, "kind": EV_CLAIM, "confidence": 500, "context": CTX_NONE, "clock": m.clock_s, "salience": 0, "valence": 0})
    m.sync(lib, s, "tell", checks)
    d = op(EXCHANGE, KYRA, 4, 2, 0x8000 | 2, at=(22000, 4300, -95200), server=0)
    check(d.status == OK and s.feed_count == feeds, "exchange is private")
    m.revision = s.revision
    m.witness((22000, 4300, -95200), EXCHANGE, occluded=(TOMA,))
    evidence_put(m.ev, {"observer": h_npc(KYRA), "subject": H_PLAYER, "teller": 0, "root": m.revision, "kind": EV_ACT, "confidence": 1000, "context": CTX_NONE, "clock": m.clock_s, "salience": 0, "valence": ACTION_VALENCE[EXCHANGE]})
    m.sync(lib, s, "exchange", checks)
    d = op(REPORT, CLERK, 5, 0, h_npc(DAX), at=(86200, 30300, 30000), server=0)
    check(d.status == OK and s.pend_count == 1, "player report filed")
    m.revision = s.revision
    m.witness((86200, 30300, 30000), REPORT)  # the clerk sees the filing too
    m.pend.append({
        "observer": h_npc(CLERK), "subject": h_npc(DAX), "teller": H_PLAYER,
        "root": m.revision, "deliver_s": m.clock_s + 3600,
        "kind": EV_CLAIM, "confidence": 500, "context": CTX_DISPATCH,
        "salience": 0, "valence": 0,
    })
    m.sync(lib, s, "reported", checks)
    lib.ws_clock_advance(C.byref(s), C.byref(r), 3600)
    m.clock_s += 3600
    social_settle(vars(m))
    m.sync(lib, s, "report delivered", checks)
    delivered = [rec for rec in m.ev if rec["observer"] == h_npc(CLERK) and rec["subject"] == h_npc(DAX)]
    check(len(delivered) == 2 and delivered[-1]["confidence"] == 375 and delivered[-1]["teller"] == H_PLAYER, "player claim stays claim-grade")

    # 6. Fail-closed parity: identical refusals and untouched tables.
    hash_before = lib.ws_state_hash(C.byref(s))
    bad = [
        (h_npc(KYRA), h_npc(DAX), 0, 0, 500, CTX_NONE, 0, 0, m.revision, BOUNDS),
        (h_npc(KYRA), h_npc(DAX), 0, EV_REPORT + 1, 500, CTX_NONE, 0, 0, m.revision, BOUNDS),
        (h_npc(KYRA), h_npc(DAX), 0, EV_ACT, 1001, CTX_NONE, 0, 0, m.revision, BOUNDS),
        (h_npc(KYRA), h_npc(DAX), 0, EV_ACT, 500, CTX_NONE, 0, 1001, m.revision, BOUNDS),
        (h_npc(KYRA), 99, 0, EV_ACT, 500, CTX_NONE, 0, 0, m.revision, REFERENCE),
        (99, h_npc(DAX), 0, EV_ACT, 500, CTX_NONE, 0, 0, m.revision, REFERENCE),
        (h_npc(KYRA), h_npc(DAX), h_npc(KYRA), EV_ACT, 500, CTX_NONE, 0, 0, m.revision, BOUNDS),
        (h_npc(KYRA), h_npc(DAX), 0, EV_ACT, 500, CTX_NONE, 0, 0, 0, REFERENCE),
        (h_npc(KYRA), h_npc(DAX), 0, EV_ACT, 500, CTX_NONE, 0, 0, m.revision + 1, REFERENCE),
    ]
    for observer, subject, teller, kind, conf, ctx, valence, sal, root_, want in bad:
        got = lib.ws_inform(C.byref(s), C.byref(r), observer, subject, teller, kind, conf, ctx, valence, sal, root_)
        check(got == want, f"inform refusal {kind}/{conf} -> {got} != {want}")
    check(lib.ws_file(C.byref(s), C.byref(r), H_PLAYER, h_npc(KYRA), h_npc(DAX)) == REFERENCE, "clerk must be an NPC")
    check(lib.ws_file(C.byref(s), C.byref(r), h_npc(CLERK), h_npc(OREN), h_npc(DAX)) == DENIED, "nothing retained to file")
    check(lib.ws_state_hash(C.byref(s)) == hash_before, "refusals transactional")
    m.sync(lib, s, "fail-closed", checks)

    # 7. Wire v4 parity: the mirror codec emits the identical bytes, the
    # mirrored payload CRC is the state hash, and bad wires fail closed.
    payload = mirror_payload(s, False)
    check(lib.ws_state_hash(C.byref(s)) == zlib.crc32(payload), "hash equals the mirrored payload crc")
    buf = (C.c_ubyte * 20000)()
    n = lib.ws_state_encode(C.byref(s), buf, 20000)
    check(n > 48 and buf[4] == 4 and buf[5] == 0, "wire v4 header")
    check(bytes(buf[:n]) == mirror_wire(s), "mirror codec emits identical wire bytes")
    t = State()
    check(lib.ws_state_decode(C.byref(t), C.byref(r), buf, n) == OK, "decode")
    check(lib.ws_state_hash(C.byref(t)) == lib.ws_state_hash(C.byref(s)), "decode hash identity")
    for i in (0, 1, 15, 47, 48, 100, n // 2, n - 1):
        check(lib.ws_state_decode(C.byref(t), C.byref(r), buf, i) != OK, f"truncated {i}")
    corrupt = bytearray(buf[:n])
    corrupt[-1] ^= 1
    check(lib.ws_state_decode(C.byref(t), C.byref(r), bytes(corrupt), n) == FORMAT, "crc mismatch fails closed")
    base = "build/social-state-py"
    for slot in (0, 1):
        pathlib.Path(f"{base}.{slot}").unlink(missing_ok=True)
    check(lib.ws_save(C.byref(s), base.encode()) == 1, "save")
    u = State()
    check(lib.ws_restore(C.byref(u), C.byref(r), base.encode()) == 1, "restore")
    check(lib.ws_state_hash(C.byref(u)) == lib.ws_state_hash(C.byref(s)), "save/restore identity")

    # 8. Generalization: the same parity over the committed Cascade product.
    cdata = (ROOT / "build/cascade.cws").read_bytes()
    cr = Recipe()
    check(lib.ws_load(C.byref(cr), cdata, len(cdata)) == OK, "cascade loads")
    cs = State()
    lib.ws_state_init(C.byref(cs), C.byref(cr))
    lib.ws_join(C.byref(cs), 100)
    cnpc = [i for i, mod in enumerate(cr.modules[: cr.count]) if mod.flags & 16]
    check(cnpc == [15], "cascade cast")
    cm = Mirror(cnpc, [100], cr.modules)
    cs.players[0].inventory[1] = 2
    dop = lib.ws_apply(
        C.byref(cs), C.byref(cr),
        Context(100, Address(Pos(22000, 4000, -98000), 0xFFFF), 1),
        Operation(1, cs.entities[4].epoch, cs.entities[4].revision, ACTIONS["REPAIR"], 4, 1, 0),
    )
    check(dop.status == OK and dop.rewarded, "cascade repair")
    cm.revision = cs.revision
    cm.witness((22000, 4000, -98000), ACTIONS["REPAIR"])
    cm.sync(lib, cs, "cascade repair", checks)
    view = social_view([rec for rec in cm.ev if rec["observer"] == h_npc(15)], cm.clock_s)
    check(view["trust"] == 270 and view["acts"] == 1, "cascade kyra arithmetic")
    # an inform sweep on the cascade product: full formation + view parity
    for k in range(6):
        cm.revision += 1
        cs.revision = cm.revision
        kind = EV_ACT if k % 2 else EV_CLAIM
        res = lib.ws_inform(C.byref(cs), C.byref(cr), h_npc(15), H_PLAYER, 0, kind, 400 + k * 100, CTX_NONE, -100 if k % 2 else 0, 0, cm.revision)
        check(res == OK, "cascade inform")
        evidence_put(cm.ev, {"observer": h_npc(15), "subject": H_PLAYER, "teller": 0, "root": cm.revision, "kind": kind, "confidence": 400 + k * 100, "context": CTX_NONE, "clock": cm.clock_s, "salience": 0, "valence": -100 if k % 2 else 0})
    cm.sync(lib, cs, "cascade informs", checks)
    n2 = lib.ws_state_encode(C.byref(cs), buf, 20000)
    check(n2 > 48 and buf[4] == 4, "cascade wire v4")
    check(bytes(buf[:n2]) == mirror_wire(cs), "cascade mirror wire identical")
    t2 = State()
    check(lib.ws_state_decode(C.byref(t2), C.byref(cr), buf, n2) == OK, "cascade decode")
    check(lib.ws_state_hash(C.byref(t2)) == lib.ws_state_hash(C.byref(cs)), "cascade decode identity")

    results["checks"] = checks[0]
    results["fixture_bytes"] = len(data)
    results["cascade_bytes"] = len(cdata)
    results["evidence"] = s.ev_count
    print(json.dumps(results))
    return 0


if __name__ == "__main__":
    sys.exit(main())
