/**
 * @file usb_descriptors.c
 * @brief TinyUSB CDC-NCM device descriptors for RP2350 USB-LAN ArtNet Node
 *
 * Provides a single CDC-NCM interface that gives the host a virtual Ethernet
 * adapter.  The host assigns a DHCP address; we respond as the DHCP server.
 */

#include "tusb.h"
#include "config.h"

/* ── String indices ──────────────────────────────────────────────────────── */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_NCM_IF,
    STRID_MAC,
    STRID_COUNT
};

/* ── Device descriptor ───────────────────────────────────────────────────── */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ── Configuration descriptor ────────────────────────────────────────────── */
#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_CDC_NCM_DESC_LEN)

#define EPNUM_NCM_NOTIF     0x81
#define EPNUM_NCM_OUT       0x02
#define EPNUM_NCM_IN        0x82

uint8_t const desc_configuration[] = {
    /* Config header */
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    /* CDC-NCM interface */
    TUD_CDC_NCM_DESCRIPTOR(0, STRID_NCM_IF, STRID_MAC,
                           EPNUM_NCM_NOTIF, 64,
                           EPNUM_NCM_OUT, EPNUM_NCM_IN, CFG_TUD_NCM_IN_NTB_MAX_SIZE, 64)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ── String descriptors ──────────────────────────────────────────────────── */

/*
 * MAC address reported via CDC-NCM.  Must match the MAC used by lwIP netif.
 * Format: 12 hex digits, no colons.
 */
#define NCM_MAC_STR   "020000000001"   /* locally administered unicast */

static char const *string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){ 0x09, 0x04 }, /* English */
    [STRID_MANUFACTURER] = USB_MANUFACTURER_STR,
    [STRID_PRODUCT]      = USB_PRODUCT_STR,
    [STRID_SERIAL]       = USB_SERIAL_STR,
    [STRID_NCM_IF]       = "ArtNet NCM Interface",
    [STRID_MAC]          = NCM_MAC_STR,
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    size_t chr_count;

    if (index == STRID_LANGID) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= STRID_COUNT) return NULL;
        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (size_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
