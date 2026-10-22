#pragma once

/* Firmware compatibility layer.
 *
 * FlipSay builds against the Momentum SDK and against the Unleashed SDK. RogueMaster is based on
 * Unleashed and uses the same API major version, so the Unleashed build is the one for it.
 * Only the things that really differ between the firmwares live here.
 */

#include <furi.h>
#include <furi_hal.h>

#if defined(__has_include)
#if __has_include(<momentum/momentum.h>)
#define FS_FW_MOMENTUM 1
#include <momentum/momentum.h>
#endif
#endif

#ifdef FS_FW_MOMENTUM
#define FS_FW_NAME "MOMENTUM"
#define FS_HAS_PIN_SELECT 1
#else
#define FS_FW_NAME "UNLEASHED / RM"
#define FS_HAS_PIN_SELECT 0
#endif

/* SPI bus handle of the external CC1101. Momentum can move it to the alternate CS pin, the other
 * firmwares always use the standard external handle (CS on pin 4). */
static inline const FuriHalSpiBusHandle* fs_fw_cc_spi(void) {
#ifdef FS_FW_MOMENTUM
    return momentum_settings.spi_cc1101_handle == SpiDefault ?
               &furi_hal_spi_bus_handle_external :
               &furi_hal_spi_bus_handle_external_extra;
#else
    return &furi_hal_spi_bus_handle_external;
#endif
}

static inline const FuriHalSpiBusHandle* fs_fw_nrf_spi(void) {
#ifdef FS_FW_MOMENTUM
    return momentum_settings.spi_nrf24_handle == SpiDefault ?
               &furi_hal_spi_bus_handle_external :
               &furi_hal_spi_bus_handle_external_extra;
#else
    return &furi_hal_spi_bus_handle_external;
#endif
}

/* 1 when the firmware routes the CC1101 / NRF24 over the alternate CS pin. */
static inline int fs_fw_cc_pin_alt(void) {
#ifdef FS_FW_MOMENTUM
    return momentum_settings.spi_cc1101_handle == SpiExtra ? 1 : 0;
#else
    return 0;
#endif
}

static inline int fs_fw_nrf_pin_alt(void) {
#ifdef FS_FW_MOMENTUM
    return momentum_settings.spi_nrf24_handle == SpiExtra ? 1 : 0;
#else
    return 0;
#endif
}

static inline void fs_fw_set_pins(int cc_alt, int nrf_alt) {
#ifdef FS_FW_MOMENTUM
    momentum_settings.spi_cc1101_handle = cc_alt ? SpiExtra : SpiDefault;
    momentum_settings.spi_nrf24_handle = nrf_alt ? SpiExtra : SpiDefault;
#else
    (void)cc_alt;
    (void)nrf_alt;
#endif
}

/* UART used by the Wi-Fi devboard. */
static inline FuriHalSerialId fs_fw_esp_uart(void) {
#ifdef FS_FW_MOMENTUM
    return momentum_settings.uart_esp_channel;
#else
    return FuriHalSerialIdUsart;
#endif
}
