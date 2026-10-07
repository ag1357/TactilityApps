"""Bounded little-endian envelopes. No socket or simulation dependency.

World/branch identity and content hash are session-bound, not deployment
locations. Future routing/partitioning keeps these message shapes unchanged.
"""
import struct

MAGIC = b"ANP1"
VERSION = 1
MAX_PACKET = 32768
HEADER = struct.Struct("<4sHHI")
HELLO, WELCOME, INPUT, SNAPSHOT, ACTION, RECEIPT, OFFLINE, ERROR = range(1, 9)
AUTH = struct.Struct("<I16s32s")
STEP = struct.Struct("<IhhHBB")
OP = struct.Struct("<QIIHHi16s16s")  # sequence, revision/epoch, opcode/item, amount, entity/scope IDs
POSE = struct.Struct("<8i")
PEER = struct.Struct("<16s16s8i")  # player, scope, pose
SNAP = struct.Struct("<IIQ16sHHI")  # ack, revision, ms, scope, bytes, peers, CRC


def frame(kind, payload=b""):
    if len(payload) > MAX_PACKET:
        raise ValueError("packet capacity")
    return HEADER.pack(MAGIC, VERSION, kind, len(payload)) + payload


def unpack_header(raw):
    magic, version, kind, length = HEADER.unpack(raw)
    if magic != MAGIC or version != VERSION or length > MAX_PACKET or kind not in range(1, 9):
        raise ValueError("protocol envelope")
    return kind, length
