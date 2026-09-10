#include "hal/timer.h"
#pragma pack(push, 1)
#include "tl_common.h"
#pragma pack(pop)
#include <stdint.h>

/* Extend the 32-bit 16 MHz hardware counter before converting to milliseconds.
 * Must be serviced at least once per hardware wrap (~268 s); diagnostic main
 * calls it every iteration and sleeps no longer than 1 s. */
uint32_t hal_millis(void) {
    static uint32_t previous_ticks, millis, remainder;
    uint32_t ticks = clock_time();
    uint32_t delta = ticks - previous_ticks;
    previous_ticks = ticks;
    millis += delta / CLOCK_16M_SYS_TIMER_CLK_1MS;
    remainder += delta % CLOCK_16M_SYS_TIMER_CLK_1MS;
    if (remainder >= CLOCK_16M_SYS_TIMER_CLK_1MS) {
        millis++;
        remainder -= CLOCK_16M_SYS_TIMER_CLK_1MS;
    }
    return millis;
}
