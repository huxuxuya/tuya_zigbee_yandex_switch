#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include "flash_guard.h"
int main(void) {
    /* All physical sectors: only our 24 sectors may be erased. */
    for (uint32_t address=0;address<0x100000;address+=4096) {
        bool ours=address>=0x80000 && address<0x98000;
        assert(ws_flash_command_allowed(0x20,address,1,0)==ours);
        assert(ws_flash_command_allowed(0x02,address,1,4096)==ours);
        assert(!ws_flash_command_allowed(0x20,address+1,1,0));
    }
    assert(ws_flash_command_allowed(0x02,0x80000,1,0x18000));
    assert(ws_flash_command_allowed(0x02,0x97fff,1,1));
    assert(!ws_flash_command_allowed(0x02,0x97fff,1,2));
    assert(!ws_flash_command_allowed(0x02,0x7ffff,1,2));
    assert(!ws_flash_command_allowed(0x02,0x80000,1,UINT32_MAX));
    assert(!ws_flash_command_allowed(0x02,UINT32_MAX,1,2));
    assert(!ws_flash_command_allowed(0x02,0x80000,1,0));
    for (unsigned command=0;command<256;command++) {
        if (command!=0x02 && command!=0x20)
            assert(!ws_flash_command_allowed(command,0x80000,1,1));
    }
    assert(ws_flash_command_allowed(0x01,0,0,1));
    assert(ws_flash_command_allowed(0x01,0,0,2));
    assert(!ws_flash_command_allowed(0x01,0,0,3));
    assert(!ws_flash_command_allowed(0x02,0x80000,0,1));
    assert(!ws_flash_command_allowed(0x02,0x80000,2,1));
    uint32_t denied=ws_flash_denied_count;
    assert(!ws_flash_command_allowed(0x42,0xff000,1,16));
    assert(ws_flash_denied_count==denied+1);
    assert(ws_flash_denied_address==0xff000 && ws_flash_denied_command==0x42);
    puts("Flash guard: 256 sectors, commands, boundaries, overflow, alignment and denial telemetry passed");
}
