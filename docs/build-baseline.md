# Build baseline

Исходный snapshot Telink-проекта находится в `firmware/upstream/`. Его commit и
контрольные суммы перечислены в `firmware/UPSTREAM.md`.

## Требования

- Docker;
- Linux/amd64 container (на Apple Silicon используется эмуляция amd64);
- SDK Telink 3.7.2.0 и TC32 GCC v2.0, полученные из разрешённых источников.

Локальные архивы SDK и toolchain не входят в публичный snapshot. Скрипт сборки
проверяет SHA-256 загруженных архивов до использования.

## Сборка upstream baseline

```sh
make -C firmware/upstream telink/tools/sdk telink/tools/toolchain
bash scripts/build_baseline.sh
```

## Диагностическая сборка

```sh
bash scripts/build_diagnostic.sh
```

Скрипт создаёт временную копию исходников в `build/diagnostic-source/`, применяет
публичный application overlay и запускает layout/audit-проверки. Все результаты
сборки находятся в игнорируемом каталоге `build/`.

Baseline и diagnostic image — разные артефакты. Baseline не предназначен для
прошивки устройства.
