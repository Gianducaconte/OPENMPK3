/* OPENMPK3 — USB descriptors (class-compliant MIDI, one port each way). */
#include "tusb.h"

static const tusb_desc_device_t dev = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE, .bcdUSB = 0x0200,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x09E8, .idProduct = 0x0049, .bcdDevice = 0x0300,   /* Akai IDs kept; 3.00 = OPENMPK3 */
    .iManufacturer = 1, .iProduct = 2, .iSerialNumber = 3, .bNumConfigurations = 1 };
uint8_t const *tud_descriptor_device_cb(void) { return (uint8_t const *)&dev; }

enum { ITF_MIDI = 0, ITF_MIDI_STREAMING, ITF_TOTAL };
#define CFG_LEN (TUD_CONFIG_DESC_LEN + TUD_MIDI_DESC_LEN)
static const uint8_t cfg[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_TOTAL, 0, CFG_LEN, 0x00, 500),
    TUD_MIDI_DESCRIPTOR(ITF_MIDI, 0, 0x01, 0x81, 64),
};
uint8_t const *tud_descriptor_configuration_cb(uint8_t i) { (void)i; return cfg; }

static const char *const str[] = { NULL, "AKAI", "OPENMPK3", "OPENMPK3-0001" };
static uint16_t buf[32];
uint16_t const *tud_descriptor_string_cb(uint8_t idx, uint16_t langid)
{
    (void)langid; uint8_t n;
    if (idx == 0) { buf[1] = 0x0409; n = 1; }
    else {
        if (idx >= sizeof str / sizeof str[0]) return NULL;
        const char *s = str[idx];
        for (n = 0; s[n] && n < 31; n++) buf[1 + n] = (uint8_t)s[n];
    }
    buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return buf;
}
