/**
 * @file pins.h
 * @brief GPIO pin definitions for RP2350 USB-LAN ArtNet Node
 *
 * Pin assignments for the RS485 DMX/RDM bus interface.
 * USB pins are fixed by hardware (DP/DM on GPIO 23/24 on Pico 2).
 */

#ifndef PINS_H
#define PINS_H

/* RS485 / DMX / RDM interface (UART1) */
#define PIN_DMX_TX          4   /* UART1 TX */
#define PIN_DMX_RX          5   /* UART1 RX */
#define PIN_DMX_DE          2   /* RS485 Driver Enable (HIGH = transmit) */
#define PIN_DMX_RE          3   /* RS485 Receiver Enable (LOW  = receive) */

/* Combined DE/RE control – pull HIGH to transmit, LOW to receive */
#define DMX_RS485_DIR_TX()  do { gpio_put(PIN_DMX_DE, 1); gpio_put(PIN_DMX_RE, 1); } while (0)
#define DMX_RS485_DIR_RX()  do { gpio_put(PIN_DMX_DE, 0); gpio_put(PIN_DMX_RE, 0); } while (0)

/* Status / activity LED */
#define PIN_LED             25  /* On-board LED on Raspberry Pi Pico 2 */

/* Optional boot button (used to trigger firmware update mode) */
#define PIN_BOOT_BUTTON     23  /* BOOTSEL button on Pico (low-active) */

#endif /* PINS_H */
