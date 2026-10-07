"""Tiny offline ZIP fixtures. No real assets or installer execution."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)

def package(version, payload, bad_hash=False):
    return {"schema_version": 1, "engine_api": 1, "id": "material-test",
            "version": version, "dependencies": [], "extensions": [],
            "assets": {"image/test.png": "image/test.png"},
            "files": [{"path": "image/test.png", "role": "data", "size": len(payload),
                       "sha256": "0" * 64 if bad_hash else hashlib.sha256(payload).hexdigest()}]}

for filename, version, payload, bad_hash in [
        ("v1.zip", "1.0.0", b"old asset", False),
        ("v2.zip", "2.0.0", b"new asset", False),
        ("bad-hash.zip", "2.0.0", b"bad asset", True)]:
    with zipfile.ZipFile(root / filename, "w") as archive:
        archive.writestr("manifest.json", json.dumps(package(version, payload, bad_hash)))
        archive.writestr("image/test.png", payload)

with zipfile.ZipFile(root / "traversal.zip", "w") as archive:
    archive.writestr("../escaped.txt", b"must not escape")
with zipfile.ZipFile(root / "symlink.zip", "w") as archive:
    member = zipfile.ZipInfo("image/test.png")
    member.create_system = 3
    member.external_attr = 0o120777 << 16
    archive.writestr(member, "../../outside")
with zipfile.ZipFile(root / "compression-bomb.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    archive.writestr("data/bomb", b"\0" * 100_000)

# Common ZIP wrapper directory remains compatible with store/catalog generation.
with zipfile.ZipFile(root / "v1-wrapped.zip", "w") as archive:
    archive.writestr("material-test/", b"")
    archive.writestr("material-test/manifest.json", json.dumps(package("1.0.0", b"old asset")))
    archive.writestr("material-test/image/test.png", b"old asset")
