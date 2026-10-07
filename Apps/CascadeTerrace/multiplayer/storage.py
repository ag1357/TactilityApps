"""Portable SQLite archive; one transaction per durable action, not per tick.

Use backup() for a consistent migration image (including WAL), never copy a
live .sqlite file alone. Storage path is configuration, not part of identity.
"""
import hashlib
import hmac
import os
import pathlib
import secrets
import sqlite3
import uuid
from .fingerprint import fingerprint


def identity():
    return uuid.uuid4().bytes


class Store:
    def __init__(self, path, seed):
        path = pathlib.Path(path)
        new = not path.exists()
        if new:
            fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            os.close(fd)
        elif path.stat().st_mode & 0o077:
            raise ValueError("database must have owner-only permissions")
        self.db = sqlite3.connect(path)
        if not new:
            try:
                meta = dict(self.db.execute("SELECT key,value FROM meta"))
                if meta.get("schema") != b"2" or meta.get("content_hash") != fingerprint():
                    raise ValueError("storage schema/content mismatch; explicit migration required")
                if int(meta["seed"]) != seed:
                    raise ValueError("storage seed mismatch")
            except Exception:
                self.db.close()
                raise
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.executescript("""
        CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value BLOB NOT NULL);
        CREATE TABLE IF NOT EXISTS accounts(account_id BLOB PRIMARY KEY);
        CREATE TABLE IF NOT EXISTS players(player_id BLOB PRIMARY KEY,account_id BLOB NOT NULL,
            state BLOB, sequence INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE IF NOT EXISTS devices(device_id BLOB PRIMARY KEY,account_id BLOB NOT NULL,
            player_id BLOB NOT NULL,scheme INTEGER NOT NULL,verifier BLOB NOT NULL,
            last_sequence INTEGER NOT NULL DEFAULT 0);
        CREATE TABLE IF NOT EXISTS sessions(session_id BLOB PRIMARY KEY,device_id BLOB NOT NULL,
            player_id BLOB NOT NULL,created INTEGER NOT NULL,expires INTEGER NOT NULL);
        CREATE TABLE IF NOT EXISTS events(revision INTEGER PRIMARY KEY,world_id BLOB NOT NULL,
            player_id BLOB NOT NULL,device_id BLOB NOT NULL,sequence INTEGER NOT NULL,
            request BLOB NOT NULL,status INTEGER NOT NULL,world BLOB NOT NULL,
            player BLOB NOT NULL,origin TEXT NOT NULL,UNIQUE(device_id,sequence));
        """)
        with self.db:
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('world_id',?)", (identity(),))
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('seed',?)", (str(seed).encode(),))
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('schema',?)", (b"2",))
            self.db.execute("INSERT OR IGNORE INTO meta VALUES('content_hash',?)", (fingerprint(),))
        actual = int(self.db.execute("SELECT value FROM meta WHERE key='seed'").fetchone()[0])
        if actual != seed:
            raise ValueError("storage seed mismatch")
        self.world_id = self.db.execute("SELECT value FROM meta WHERE key='world_id'").fetchone()[0]

    def enrol(self):
        account, player, device, credential = identity(), identity(), identity(), secrets.token_bytes(32)
        with self.db:
            self.db.execute("INSERT INTO accounts VALUES(?)", (account,))
            self.db.execute("INSERT INTO players(player_id,account_id) VALUES(?,?)", (player, account))
            self.db.execute("INSERT INTO devices(device_id,account_id,player_id,scheme,verifier) VALUES(?,?,?,?,?)",
                            (device, account, player, 1, hashlib.sha256(credential).digest()))
        return account, player, device, credential

    def login(self, device, credential, now, ttl):
        row = self.db.execute("SELECT account_id,player_id,verifier FROM devices WHERE device_id=? AND scheme=1",
                              (device,)).fetchone()
        if not row or not hmac.compare_digest(row[2], hashlib.sha256(credential).digest()):
            raise ValueError("authentication failed")
        session = identity()
        with self.db:
            self.db.execute("DELETE FROM sessions WHERE expires<?", (now,))
            self.db.execute("INSERT INTO sessions VALUES(?,?,?,?,?)",
                            (session, device, row[1], now, now + ttl))
        return row[0], row[1], session

    def player(self, player):
        return self.db.execute("SELECT state,sequence FROM players WHERE player_id=?", (player,)).fetchone()

    def device_sequence(self, device):
        return self.db.execute("SELECT last_sequence FROM devices WHERE device_id=?", (device,)).fetchone()[0]

    def latest(self):
        return self.db.execute("SELECT revision,world FROM events ORDER BY revision DESC LIMIT 1").fetchone()

    def receipt(self, device, sequence, request):
        row = self.db.execute("SELECT request,status,revision FROM events WHERE device_id=? AND sequence=?",
                              (device, sequence)).fetchone()
        if row and row[0] != request:
            raise ValueError("sequence reused with different request")
        return None if row is None else (row[1], row[2])

    def commit(self, revision, player, device, sequence, request, status, world, state, origin):
        with self.db:
            self.db.execute("INSERT INTO events VALUES(?,?,?,?,?,?,?,?,?,?)",
                (revision, self.world_id, player, device, sequence, request, status, world, state, origin))
            self.db.execute("UPDATE players SET state=?,sequence=? WHERE player_id=?", (state, sequence, player))
            self.db.execute("UPDATE devices SET last_sequence=? WHERE device_id=?", (sequence, device))

    def checkpoint_world(self, state):
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO meta VALUES('world_checkpoint',?)", (state,))
            row = self.db.execute("SELECT max(revision) FROM events").fetchone()
            self.db.execute("INSERT OR REPLACE INTO meta VALUES('checkpoint_revision',?)", (str(row[0] or 0).encode(),))

    def world_checkpoint(self):
        row = self.db.execute("SELECT value FROM meta WHERE key='world_checkpoint'").fetchone()
        if not row:
            return None
        revision = int(self.db.execute("SELECT value FROM meta WHERE key='checkpoint_revision'").fetchone()[0])
        return revision, row[0]

    def checkpoint(self, states, world=None):
        with self.db:
            for player, state in states:
                self.db.execute("UPDATE players SET state=? WHERE player_id=?", (state, player))
            if world is not None:
                self.db.execute("INSERT OR REPLACE INTO meta VALUES('world_checkpoint',?)", (world,))
                row = self.db.execute("SELECT max(revision) FROM events").fetchone()
                self.db.execute("INSERT OR REPLACE INTO meta VALUES('checkpoint_revision',?)", (str(row[0] or 0).encode(),))

    def backup(self, destination):
        # Never overwrite an existing migration destination.
        fd = os.open(destination, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        os.close(fd)
        other = sqlite3.connect(destination)
        self.db.backup(other)
        other.close()

    def close(self):
        self.db.close()
