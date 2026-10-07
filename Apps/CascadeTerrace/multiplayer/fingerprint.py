"""Fingerprint the existing canonical product bytes, not source formatting."""
import hashlib
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]


def fingerprint():
    text = (ROOT / "content/worlds/cascade.inc").read_text()
    data = bytes(int(n) for n in re.findall(r"\d+", text.split("{", 1)[1].split("}", 1)[0]))
    return hashlib.sha256(data).digest()


if __name__ == "__main__":
    print("static const unsigned char mp_content_hash[32]={" +
          ",".join(str(n) for n in fingerprint()) + "};")
