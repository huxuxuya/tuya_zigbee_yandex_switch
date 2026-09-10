#include "flash_guard.h"
volatile uint32_t ws_flash_denied_count;
volatile uint32_t ws_flash_denied_address;
volatile uint32_t ws_flash_denied_command;
/* Called before SPI write-enable, while flash is still readable. Keep in RAM
 * alongside the SDK write routine. No GPIO, logging, allocation or NV here. */
#ifdef HAL_TELINK
__attribute__((section(".ram_code"),noinline))
#endif
bool ws_flash_command_allowed(uint8_t command, uint32_t address, uint8_t address_enabled, uint32_t length) {
    /* SDK protection-register operations; no flash-array or security-OTP write. */
    if (command==0x01 && !address_enabled && (length==1 || length==2)) return true;
    bool allowed=false;
    if (address_enabled==1 && address>=WS_NV_START && address<WS_STORAGE_END) {
        if (command==0x02)
            allowed=length>0 && length<=WS_STORAGE_END-address;
        else if (command==0x20)
            allowed=length==0 && !(address&0xfffu) && 0x1000u<=WS_STORAGE_END-address;
    }
    if (!allowed) {
        ws_flash_denied_count++;
        ws_flash_denied_address=address;
        ws_flash_denied_command=command;
    }
    return allowed;
}
