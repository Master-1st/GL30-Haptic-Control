"""Verify the evidence-derived H25 project source identity without hardware or build tools."""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
EXPECTED_BINARY = "B98161DB5EDAE02EBCE29F0F645405C2DA73CC1B99387A67D55181E98964DFB7"

def main():
    manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8-sig"))
    if manifest["Format"] != "SHA256_UTF8_LF" or manifest["BinarySHA256"] != EXPECTED_BINARY:
        raise ValueError("Not the qualified H25 identity format/image")
    sources = manifest["Sources"]
    if len(sources) != 37 or len({s["Path"] for s in sources}) != 37:
        raise ValueError("Expected 37 distinct acceptance inputs")
    for source in sources:
        path = (ROOT / "firmware-stm32" / source["Path"]).resolve()
        if not path.is_relative_to(ROOT / "firmware-stm32"):
            raise ValueError("Source outside firmware tree")
        data = path.read_bytes().decode("utf-8").replace("\r\n", "\n").encode("utf-8")
        if hashlib.sha256(data).hexdigest().upper() != source["SHA256"]:
            raise ValueError("Source changed: " + source["Path"])
    print("H25_SOURCE_IDENTITY_PASS: 37 files; UTF-8/LF canonical SHA-256")

if __name__ == "__main__":
    main()
