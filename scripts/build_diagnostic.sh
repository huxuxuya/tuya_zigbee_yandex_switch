#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$project_dir/scripts/prepare_diagnostic.py"
python3 "$project_dir/scripts/audit_diagnostic_layout.py"
docker run --rm --platform linux/amd64 --network none \
  --mount "type=bind,source=$project_dir,target=/workspace" \
  --workdir /workspace/build/diagnostic-source wall-switch-build:bookworm \
  sh -ec 'make -j4 telink/build DEVICE_TYPE=end_device DEBUG=0 FIRMWARE_BASENAME=yandex_diagnostic CONFIG_STR="WallSwitch;Diagnostic;" LNK_FLAGS="--gc-sections -nostartfiles -Map=../../build/telink/diagnostic.map"; telink_tools/toolchain/tc32/bin/tc32-elf-nm build/telink/yandex_diagnostic.elf > build/telink/symbols.txt; make -C src/telink -f layout_probe.mk layout-probe DEVICE_TYPE=end_device DEBUG=0 FIRMWARE_BASENAME=yandex_diagnostic CONFIG_STR="WallSwitch;Diagnostic;"'

python3 "$project_dir/scripts/audit_diagnostic_layout.py" --decode
python3 "$project_dir/scripts/verify_diagnostic.py"
