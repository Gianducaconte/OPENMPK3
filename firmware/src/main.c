/* OPENMPK3 — main loop. */
#include "stm32f0.h"
#include "tusb.h"
#include "board.h"
#include "inputs.h"
#include "app.h"

void usb_init(void);
void app_report_loop(uint32_t us);
void app_midi_flush(void);

int main(void)
{
    clock_init();
    board_init();
    inputs_init();
    app_init();
    usb_init();
    ui_boot();
    uint32_t last_draw = 0;
    for (;;) {
        uint32_t t0 = g_ticks;
        tud_task();
        inputs_poll();
        input_ev_t e;
        while (inputs_pop(&e)) app_event(&e);
        uint8_t pkt[4];
        while (tud_midi_available() && tud_midi_packet_read(pkt)) app_midi_in(pkt);
        app_poll();
        app_midi_flush();
        if (g_ms - last_draw >= 40) { last_draw = g_ms; ui_draw(); }
        app_report_loop((g_ticks - t0) * 20);
    }
}
