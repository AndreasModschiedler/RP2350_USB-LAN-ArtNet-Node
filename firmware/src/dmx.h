/**
 * @file dmx.h
 * @brief DMX-512 output driver using RP2350 UART1
 */

#ifndef DMX_H
#define DMX_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/** Opaque frame buffer: one start code byte + 512 channel values */
typedef struct {
    uint8_t data[1 + DMX_CHANNELS];  /* data[0] = start code (0x00 for DMX) */
    uint16_t channel_count;           /* number of channels in use (1..512) */
} dmx_frame_buffer_t;

/** Initialise UART1, RS485 direction pins, and GPIO for DMX output.
 *  Call once from main() before calling rdm_init(). */
void dmx_init(void);

/** Copy new channel values into the frame buffer.
 *  Uses a double-buffer swap for tear-free updates.
 *
 *  @param data   Channel values array (index 0 = channel 1)
 *  @param count  Number of channels to update (max DMX_CHANNELS)
 */
void dmx_update(const uint8_t *data, uint16_t count);

/** Start continuous DMX transmission. */
void dmx_start(void);

/** Stop DMX transmission (line stays HIGH / idle). */
void dmx_stop(void);

/** Must be called regularly from the main loop to trigger the next frame
 *  when the configured refresh interval has elapsed. */
void dmx_task(void);

/** Set the target frame rate (Hz).  Accepted range: 1 – 44. */
void dmx_set_rate(uint8_t hz);

#endif /* DMX_H */
