#!/usr/bin/env python3
"""Two normal desktop games over real loopback TCP, not the SDK viewer."""
import json
import os
import pathlib
import signal
import subprocess
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    results = ROOT / "results/multiplayer"
    results.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mp-games-", dir=ROOT / "build") as temporary:
        work = pathlib.Path(temporary)
        server = subprocess.Popen(
            ["python3", "-u", "-m", "multiplayer.server", "--database", str(work / "world.sqlite"),
             "--port", "0", "--dev-enrolment", "--metrics", str(work / "server.json")],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        clients = []
        try:
            ready = json.loads(server.stdout.readline())
            if not ready.get("ready"):
                raise RuntimeError("authority did not become ready")
            for name in ("alice", "bob"):
                replay = work / (name + ".script")
                replay.write_text("idle 1000\nlook " + ("0" if name == "alice" else "90") +
                    "\nhold w 3000\nidle " + ("50000" if name == "alice" else "52000") +
                    "\ncondense 1\nidle 1000\nsnapshot " + str(work / (name + ".ppm")) + "\nquit\n")
                clients.append(subprocess.Popen(
                    [str(ROOT / "build/cascade"), "--headless", "--server", "127.0.0.1",
                     "--port", str(ready["port"]), "--script", str(replay),
                     "--save", str(work / (name + ".save")), "--identity", str(work / (name + ".keys")),
                     "--network-report", str(work / (name + ".json"))],
                    cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
            for client in clients:
                stdout, stderr = client.communicate(timeout=75)
                if client.returncode:
                    raise RuntimeError(stderr or stdout)
            reports = [json.loads((work / (name + ".json")).read_text()) for name in ("alice", "bob")]
            for report in reports:
                assert report["snapshots"] > 20 and report["peers_seen_max"] == 1
                assert report["reconciliations"] > 20 and report["queue_overflow"] == 0
                assert report["successful_actions"] >= 1
                assert any(report["authority_start_mm"][i] != report["authority_last_mm"][i] for i in (0, 2))
            server.send_signal(signal.SIGTERM)
            stdout, stderr = server.communicate(timeout=25)
            if server.returncode:
                raise RuntimeError(stderr or stdout)
            record = {"status": "PASS", "scope": "Two normal desktop game clients, not SDK viewer",
                      "clients": reports, "server": json.loads((work / "server.json").read_text())}
            (results / "normal-games.json").write_text(json.dumps(record, indent=2) + "\n")
            print(json.dumps(record, indent=2))
        finally:
            for client in clients:
                if client.poll() is None:
                    client.terminate()
                    client.wait(timeout=5)
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=25)


if __name__ == "__main__":
    main()
