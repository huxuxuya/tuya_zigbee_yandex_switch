#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
app_dir="$project_dir/firmware/application"
mkdir -p "$project_dir/build/application"
set --
case "${SANITIZE:-none}" in
  none) ;;
  undefined) set -- -fsanitize=undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -g ;;
  address,undefined) set -- -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all -g ;;
  *) echo "SANITIZE must be none, undefined, or address,undefined" >&2; exit 2 ;;
esac
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic \
  "$@" \
  -I"$app_dir" "$app_dir/gestures.c" "$app_dir/tests/test_gestures.c" \
  -o "$project_dir/build/application/test_gestures"
"$project_dir/build/application/test_gestures"
if [ -f "$project_dir/build/yandex-analysis/tag-0000.bin" ]; then
  python3 "$project_dir/scripts/ghidra/export_mode_fixture.py"
  python3 "$project_dir/scripts/ghidra/export_button_fixture.py"
  "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter \
    "$@" -DWS_ORIGINAL_MODE_FIXTURE -I"$project_dir/build/application" -DHAL_STUB -I"$app_dir" -I"$project_dir/firmware/upstream/src" \
    "$app_dir/gestures.c" "$app_dir/button_adapter.c" "$app_dir/yandex_diagnostic.c" "$app_dir/rejoin.c" \
    "$project_dir/firmware/upstream/src/base_components/button.c" \
    "$app_dir/tests/test_diagnostic.c" -o "$project_dir/build/application/test_diagnostic"
  "$project_dir/build/application/test_diagnostic"
else
  echo "Skipping original-code comparison tests: private reference fixture is absent"
fi
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic -Wno-strict-prototypes "$@" \
  -I"$app_dir/tests/fake_sdk" -I"$project_dir/firmware/upstream/src" \
  "$app_dir/telink/timer.c" "$app_dir/telink/zcl_yandex.c" \
  "$app_dir/tests/test_telink_boundary.c" -o "$project_dir/build/application/test_telink_boundary"
"$project_dir/build/application/test_telink_boundary"
if [ -f "$project_dir/build/application/original_endpoint_cases.h" ]; then
  "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic "$@" \
    -I"$project_dir/build/application" \
    "$app_dir/tests/test_endpoint_registry.c" -o "$project_dir/build/application/test_endpoint_registry"
  "$project_dir/build/application/test_endpoint_registry"
else
  echo "Skipping endpoint fixture tests: private reference fixture is absent"
fi
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic "$@" -I"$app_dir" \
  "$app_dir/rejoin.c" "$app_dir/tests/test_rejoin.c" -o "$project_dir/build/application/test_rejoin"
"$project_dir/build/application/test_rejoin"
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic "$@" \
  "$app_dir/tests/test_network_rejoin.c" -o "$project_dir/build/application/test_network_rejoin"
"$project_dir/build/application/test_network_rejoin"

"${CC:-cc}" -std=c99 -Wall -Wextra -Werror "$@" -I"$app_dir" \
  "$app_dir/flash_guard.c" "$app_dir/tests/test_flash_guard.c" \
  -o "$project_dir/build/application/test_flash_guard"
"$project_dir/build/application/test_flash_guard"
