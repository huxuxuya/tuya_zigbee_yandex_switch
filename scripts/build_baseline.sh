#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
upstream_dir="$project_dir/firmware/upstream"
mkdir -p "$project_dir/build"
python3 - "$upstream_dir" <<'PY'
import hashlib
import pathlib
import sys

downloads = pathlib.Path(sys.argv[1]) / "telink_tools/downloads"
expected = {
    "V3.7.2.0.zip": "77ca35173fc9d6c4fc8a6f6a97bd601d66c7a607a5c89e3012c138736ae0c049",
    "tc32_gcc_v2.0.tar.bz2": "33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430",
}
for name, digest in expected.items():
    path = downloads / name
    if not path.is_file():
        sys.exit(f"Missing {path}; run the preparation command in docs/build-baseline.md")
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        sys.exit(f"SHA-256 mismatch: {path}")
PY
docker build --platform linux/amd64 -t wall-switch-build:bookworm "$project_dir/build-support"
docker run --rm --platform linux/amd64 --network none \
  --mount "type=bind,source=$upstream_dir,target=/workspace" \
  --workdir /workspace wall-switch-build:bookworm \
  sh -ec 'make telink/clean; make -j4 telink/build DEVICE_TYPE=end_device CONFIG_STR="WallSwitch;BuildOnly;" LNK_FLAGS="--gc-sections -nostartfiles -Map=../../build/telink/baseline.map"'
python3 - "$project_dir" <<'PY'
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
binary = root / "firmware/upstream/build/telink/bin/tlc_switch.bin"
data = binary.read_bytes()
report = {
    "purpose": "compile-only baseline, NOT APPROVED FOR FLASHING",
    "upstream_commit": "bf1059ee4c029e320a97fbfa6b07bd6ce4aa1702",
    "device_type": "end_device",
    "config": "WallSwitch;BuildOnly;",
    "size": len(data),
    "sha256": hashlib.sha256(data).hexdigest(),
}
(root / "build/baseline.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report, indent=2))
PY
