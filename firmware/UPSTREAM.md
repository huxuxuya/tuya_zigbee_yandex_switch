# Upstream snapshot

- Repository: https://github.com/romasku/tuya-zigbee-switch
- Commit: `bf1059ee4c029e320a97fbfa6b07bd6ce4aa1702`
- Retrieved: 2026-09-08
- Copy: `upstream/`, excluding `.git/` and prebuilt `bin/` artifacts.
- License retained at `upstream/LICENSE`.
- Original file hashes recorded in `upstream-files.sha256.json` (257 files); use this manifest to distinguish future local changes from the snapshot.
- SDK selected by upstream: Telink Zigbee SDK `3.7.2.0`.
- Downloaded SDK archive SHA-256: `77ca35173fc9d6c4fc8a6f6a97bd601d66c7a607a5c89e3012c138736ae0c049`.
- Toolchain archive: TC32 GCC v2.0, SHA-256 `33b854be3e3db3dba4b4dacdda2cd4ea1c94dfd4d562864a095956de7991b430`.

This is a source/build baseline, not a firmware approved for installation on ZT3L. No Yandex compatibility changes have been applied yet. Build artifacts must not be flashed: upstream includes automatic flash-layout migration and OTA, and the target board profile is not electrically verified.

Local diagnostic work lives in `application/`. `scripts/prepare_diagnostic.py` creates a disposable overlay under `build/diagnostic-source` and verifies this snapshot before copying it. The original snapshot is unchanged; see [diagnostic status](../docs/diagnostic-firmware.md).
