#!/usr/bin/env python3
"""Prepare or decode a TC32 compile-time probe using the generated target Makefile.
No hardware access, no flash commands, no raw device secrets in the report.
"""
import argparse, hashlib, json, struct
from pathlib import Path
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--decode', action='store_true')
args = parser.parse_args()
stage = root / 'build/diagnostic-source'
fields = ['BOOT_LOADER_MODE', 'IMAGE_OFFSET', 'FLASH_CAP_SIZE_1M',
          'NV_BASE_ADDRESS', 'CFG_PRE_INSTALL_CODE', 'CFG_FACTORY_RST_CNT',
          'FLASH_ADDR_OF_OTA_IMAGE', 'FLASH_OTA_IMAGE_MAX_SIZE', 'UART_PRINTF_MODE', 'PM_ENABLE']
for port in 'ABCD':
    for pin in range(8):
        fields += [f'P{port}{pin}_OUTPUT_ENABLE', f'P{port}{pin}_INPUT_ENABLE',
                   f'P{port}{pin}_DATA_OUT', f'P{port}{pin}_FUNC', f'PULL_WAKEUP_SRC_P{port}{pin}']
fields += [f'MODULE_SECT_{edge}({module},{slot})' for module in range(8) for slot in range(2) for edge in ('START','END')]
fields += ['AS_GPIO', 'AS_SWIRE', 'PM_PIN_PULLUP_1M']
if not args.decode:
    source = '#include "tl_common.h"\n#include "zb_common.h"\n#ifndef FLASH_CAP_SIZE_1M\n#define FLASH_CAP_SIZE_1M 0\n#endif\n'
    source += 'const unsigned int probe[] __attribute__((section(".ws_probe"),used)) = {\n'
    source += ',\n'.join(fields) + '\n};\n'
    (stage/'src/telink/layout_probe.c').write_text(source)
    (stage/'src/telink/layout_probe.mk').write_text('''include Makefile
.PHONY: layout-probe
layout-probe:
\t$(CC) $(GCC_FLAGS) $(DEVICE_DEFS) $(INCLUDE_PATHS) -c layout_probe.c -o ../../build/telink/layout_probe.o
\t$(OBJCOPY) -O binary -j .ws_probe ../../build/telink/layout_probe.o ../../build/telink/layout_probe.bin
\t$(OBJDUMP) -h ../../build/telink/yandex_diagnostic.elf > ../../build/telink/sections.txt
''')
else:
    data=(stage/'build/telink/layout_probe.bin').read_bytes()
    values=dict(zip(fields,struct.unpack('<'+'I'*len(fields),data)))
    dump=(root/'dumps/zt3l_stock_1.bin').read_bytes()
    second=(root/'dumps/zt3l_stock_2.bin').read_bytes()
    if len(dump)!=0x100000 or dump!=second: raise SystemExit('Stock backup mismatch')
    if any(value!=255 for value in dump[0x80000:0x98000]): raise SystemExit('Candidate storage is occupied in stock backup')
    regions=[]
    # Aggregate occupancy only: never dump MAC, keys or factory bytes.
    for start,end in [(0,0x8000),(0x8000,0x42000),(0x34000,0x40000),
                      (0x6a000,0x80000),(0x80000,0x96000),(0xe6000,0xfc000),
                      (0xfc000,0xfd000),(0xfd000,0xfe000),(0xfe000,0xff000),(0xff000,0x100000)]:
        block=dump[start:end]
        regions.append({'start':hex(start),'end_exclusive':hex(end),'non_ff_bytes':sum(x!=255 for x in block)})
    headers=[]
    for module in range(8):
        sector_size=0x4000 if module==7 else 0x1000
        for slot in range(2):
            address=0xe6000+0x2000*module+slot*sector_size
            flag,identifier,operation=struct.unpack_from('<HBB',dump,address)
            headers.append({'address':hex(address),'sdk_module':module,'slot':slot,
                            'flag':hex(flag),'module_id_matches':identifier==module,
                            'slot_matches':(operation&3)==slot})
    report={'scope' :'TC32 compile-time values; stock occupancy is not semantic NV identification',
            'image_sha256':hashlib.sha256((stage/'build/telink/bin/yandex_diagnostic.bin').read_bytes()).hexdigest(),
            'values':values,'stock_regions':regions,
            'stock_headers_interpreted_as_sdk_1m_bootloader_nv':headers,
            'candidate_storage_erased_in_both_backups':True,
            'stock_sha256':hashlib.sha256(dump).hexdigest(),'hardware_tested':False,'ready_to_flash':False}
    (root/'build/diagnostic-layout.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in values.items() if k in fields[:10]},indent=2))
    print('Report: build/diagnostic-layout.json')
