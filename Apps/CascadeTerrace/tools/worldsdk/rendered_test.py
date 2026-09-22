#!/usr/bin/env python3
"""Run two rendered clients against a real server; preserve machine results."""

import json
import subprocess
import sys
import tempfile
from qualify import ROOT


def main():
    with tempfile.TemporaryDirectory(prefix="ct-rendered-") as tmp:
        server = subprocess.Popen(
            [
                sys.executable,
                str(ROOT / "tools/worldsdk/server.py"),
                str(ROOT / "build/cascade.cws"),
                "--save",
                tmp + "/s",
                "--port",
                "0",
            ],
            stdout=subprocess.PIPE,
            text=True,
        )
        clients = []
        try:
            port = json.loads(server.stdout.readline())["port"]
            for i in range(2):
                args = [
                    sys.executable,
                    str(ROOT / "tools/worldsdk/client.py"),
                    str(ROOT / "build/cascade.cws"),
                    "--port",
                    str(port),
                    "--headless",
                    "--frames",
                    "120",
                    "--wait-peers",
                    "2",
                    "--extract",
                    "--report",
                    str(ROOT / f"results/worldsdk/client-{i + 1}.json"),
                ]
                if i == 0:
                    args += ["--repair", "--auto-forward"]
                clients.append(
                    subprocess.Popen(
                        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
                    )
                )
            results = []
            for client in clients:
                out, err = client.communicate(timeout=30)
                if client.returncode:
                    raise RuntimeError(err)
                result = json.loads(out)
                assert result["frames"] == 120 and result["peers_seen"]
                assert all(action["status"] == 0 for action in result["actions"])
                results.append(result)
            assert results[0]["public_hash"] == results[1]["public_hash"]
            assert results[0]["final_position"] != results[1]["final_position"]
            print(
                json.dumps(
                    {
                        "status": "PASS",
                        "rendered_clients": 2,
                        "frames_each": 120,
                        "independent_positions": True,
                        "peers_rendered": True,
                        "public_hash_equal": True,
                    }
                )
            )
        finally:
            for client in clients:
                if client.poll() is None:
                    client.terminate()
                    client.wait()
            server.terminate()
            server.wait()


if __name__ == "__main__":
    main()
