#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
build=root/'build/diagnostic-source/build/telink'
symbols={line.split()[-1] for line in (build/'symbols.txt').read_text().splitlines() if line.split()}
forbidden={'relay_on','relay_off','relay_toggle','ensure_correct_ota_scheme',
           'hal_zigbee_init_ota','ota_queryStart','zcl_ota_register','parse_config',
           'handle_version_changes'}
found=forbidden & symbols
if found: raise SystemExit(f'Unexpected linked functions: {sorted(found)}')
required={'ws_diag_init','ws_diag_gesture','ws_diag_inject_event','zcl_yandex_register',
          'zcl_diagnostic_onoff_register','hal_millis', 'ws_diag_endpoint_active',
          'ws_diag_platform_endpoints_changed','ws_diag_endpoint_registration_failures',
          'ws_diag_rejoin','ws_rejoin_poll','ws_rejoin_changed','bdb_isIdle','ws_flash_command_allowed','ws_flash_denied_count'}
required |= {'ws_diag_led_init','ws_diag_led_set'}
required |= {'ws_diag_relay_init'}
if not required <= symbols: raise SystemExit(f'Missing diagnostic code: {required-symbols}')
binary=build/'bin/yandex_diagnostic.bin'
layout=json.loads((root/'build/diagnostic-layout.json').read_text())
values=layout['values']
expected={'BOOT_LOADER_MODE':1,'IMAGE_OFFSET':0x8000,'FLASH_CAP_SIZE_1M':1,
          'NV_BASE_ADDRESS':0x80000,'CFG_PRE_INSTALL_CODE':0x97000,
          'CFG_FACTORY_RST_CNT':0x96000,'UART_PRINTF_MODE':0}
for key,value in expected.items():
    if values[key]!=value: raise SystemExit(f'Unexpected layout {key}: {values[key]}')
for module in range(8):
    for slot in range(2):
        start=values[f'MODULE_SECT_START({module},{slot})']
        end=values[f'MODULE_SECT_END({module},{slot})']
        if not 0x80000<=start<end<=0x96000: raise SystemExit('NV module outside isolated area')
if not layout['candidate_storage_erased_in_both_backups']: raise SystemExit('Stock candidate is occupied')
if any(value for key,value in values.items() if key.endswith('_OUTPUT_ENABLE')):
    raise SystemExit('Unexpected enabled startup GPIO output')
if values['PA7_FUNC']!=values['AS_SWIRE']:
    raise SystemExit('SWire pin function changed')
image_bytes=binary.read_bytes()
if image_bytes[8:12]!=b'KNLT' or int.from_bytes(image_bytes[24:28],'little')!=len(image_bytes):
    raise SystemExit('Invalid Telink image marker or declared length')
symbol_values={parts[-1]:int(parts[0],16) for line in (build/'symbols.txt').read_text().splitlines()
               if len(parts:=line.split())==3}
if int.from_bytes(image_bytes[12:14],'little')!=symbol_values['_ramcode_size_div_16_align_256_']:
    raise SystemExit('Image RAM-code length does not match ELF')
image_hash=hashlib.sha256(image_bytes).hexdigest()
if layout['image_sha256']!=image_hash: raise SystemExit('Stale layout report')
# Verify the linked code offset, not just a configuration header.
sections=(build/'sections.txt').read_text().splitlines()
text_section=next(line.split() for line in sections if len(line.split())>1 and line.split()[1]=='.text')
if int(text_section[3],16)-int(text_section[4],16)!=0x8000:
    raise SystemExit('ELF text VMA/LMA offset is not 0x8000')
if 0x8000+binary.stat().st_size>0x77000:
    raise SystemExit('Image overlaps SDK OTA slot')
report={'purpose':'Diagnostic build with physical buttons, Tuya indicator LEDs and latching relay outputs',
        'physical_gpio_enabled':True,'physical_button_inputs':['PC3','PD2'],
        'physical_led_outputs':['PB7','PD7'],'physical_led_active_level':1,
        'physical_factory_reset_hold_ms':10000,
        'relay_driver_linked':True,'relay_outputs':['PC2','PD4','PB5','PC4'],
        'relay_pulse_ms':100,'relay_active_level':1,'ota_client_enabled':False,
        'upstream_flash_migration_linked':False,'sdk_flash_writes_possible':True,
        'dynamic_button_endpoints':True,'mode_change_rejoin_implemented':True,
        'application_flash_base':'0x8000','flash_capacity_bytes':0x100000,
        'nv_base':'0x80000','nv_reset_flag':'0x96000','install_code':'0x97000',
        'flash_array_write_guard_range':['0x80000','0x98000'],
        'flash_guard_end_exclusive':True,'startup_gpio_outputs_enabled':False,
        'startup_led_test_enabled':False,
        'stock_bootloader_and_nv_compatibility_verified':False,
        'hardware_and_midi_tested':False,'size':binary.stat().st_size,
        'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),
        'forbidden_symbols_absent':sorted(forbidden)}
(root/'build/diagnostic.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
