# Диагностическая прошивка

Это экспериментальный TC32-образ для Tuya ZT3L на Telink TLSR825x. Он нужен для
проверки Zigbee-дескрипторов, жестов, режимов endpoint-ов, LED-индикатора и
сохранения настроек до включения силовой части.

## Реализовано

- профиль End Device с моделью `YNDX-00532`;
- два входа кнопок и программная обработка single/double/long;
- исследуемые Zigbee endpoint-ы и manufacturer cluster `0xFC03`;
- `switchMode`, `powerType`, `ledIndicator`, `startUpOnOff` и ограниченные
  `onTime/offWaitTime`;
- сохранение диагностических настроек в отдельной NV-области;
- защитный отказ от записи за пределами разрешённого диапазона;
- отдельные host-тесты и compile-time проверки Telink layout.

Текущие ограничения и статус подтверждений находятся в
[public-status.md](public-status.md). Код не следует считать готовой заменой
штатной прошивке.

## Изоляция

`scripts/prepare_diagnostic.py` не изменяет `firmware/upstream/` напрямую. Он
создаёт временную копию, проверяет upstream manifest и накладывает application
overlay. OTA и автоматические миграции диагностического target отключены.

## Проверки

```sh
SANITIZE=undefined ./scripts/test_application.sh
./scripts/build_diagnostic.sh
```

При отсутствии закрытых reference fixtures скрипт запускает публичные portable и
Telink-boundary тесты, а сравнения с оригинальным машинным кодом пропускает.
Host-тесты не моделируют радио, питание, ISR, flash wear, силовые реле или
поведение Алисы. Аппаратную прошивку выполнять только по отдельной процедуре
безопасности и после сохранения собственного дампа устройства.
