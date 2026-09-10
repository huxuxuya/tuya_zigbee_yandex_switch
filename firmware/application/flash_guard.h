#ifndef WS_FLASH_GUARD_H
#define WS_FLASH_GUARD_H
#include <stdbool.h>
#include <stdint.h>
#define WS_NV_START 0x80000u
#define WS_NV_END 0x96000u
#define WS_RESET_START 0x96000u
#define WS_INSTALL_START 0x97000u
#define WS_STORAGE_END 0x98000u
extern volatile uint32_t ws_flash_denied_count;
extern volatile uint32_t ws_flash_denied_address;
extern volatile uint32_t ws_flash_denied_command;
bool ws_flash_command_allowed(uint8_t command, uint32_t address, uint8_t address_enabled, uint32_t length);
#endif
