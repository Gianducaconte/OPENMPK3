/* OPENMPK3 — TinyUSB glue: IRQ + board hooks. */
#include "stm32f0.h"
#include "tusb.h"
#include "board.h"

void USB_IRQHandler(void) { tud_int_handler(0); }
uint32_t tusb_time_millis_api(void) { return g_ms; }

void usb_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_USBEN;
    /* the Akai bootloader may have left the device attached: drop the pull-up so the host re-enumerates */
    USB->BCDR &= ~USB_BCDR_DPPU;
    for (uint32_t t = g_ms; g_ms - t < 50; ) { }
    tusb_rhport_init_t dev = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    tusb_init(0, &dev);
}
