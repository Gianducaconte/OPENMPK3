/* OPENMPK3 — board support: matrix scan, ADC/mux, LED shift registers, OLED. Timing mirrors stock fw. */
#include "stm32f0.h"
#include "board.h"
#include <string.h>

volatile uint32_t g_ticks;

/* ------------------------------------------------------------------ GPIO helpers */
static void pin_mode(GPIO_TypeDef *g, int pin, uint32_t mode, uint32_t pupd)
{
    g->MODER = (g->MODER & ~(3u << (2 * pin))) | (mode << (2 * pin));
    g->PUPDR = (g->PUPDR & ~(3u << (2 * pin))) | (pupd << (2 * pin));
    g->OSPEEDR |= 3u << (2 * pin);
}
static void pin_af(GPIO_TypeDef *g, int pin, uint32_t af)
{
    pin_mode(g, pin, 2, 0);
    g->AFR[pin >> 3] = (g->AFR[pin >> 3] & ~(0xFu << (4 * (pin & 7)))) | (af << (4 * (pin & 7)));
}
#define HI(g, p) ((g)->BSRR = 1u << (p))
#define LO(g, p) ((g)->BRR = 1u << (p))
enum { IN = 0, OUT = 1, ANALOG = 3, NOPULL = 0, PULLUP = 1 };

/* ------------------------------------------------------------------ matrix (10 drive x 8 sense) */
#define MXQ 64
static mx_event_t mxq[MXQ];
static volatile uint8_t mx_head, mx_tail;
static uint8_t mx_state[10], mx_line;

bool matrix_pop(mx_event_t *e)
{
    if (mx_tail == mx_head) return false;
    *e = mxq[mx_tail]; mx_tail = (uint8_t)((mx_tail + 1) % MXQ);
    return true;
}

static inline void matrix_step(void)
{
    uint8_t bits = (uint8_t)~(GPIOC->IDR >> 8);          /* 1 = closed */
    uint8_t ch = bits ^ mx_state[mx_line];
    if (ch) {
        mx_state[mx_line] = bits;
        uint8_t nh = (uint8_t)((mx_head + 1) % MXQ);
        if (nh != mx_tail) { mxq[mx_head] = (mx_event_t){ mx_line, bits, ch, g_ticks }; mx_head = nh; }
    }
    GPIOC->BSRR = 0x00FF; GPIOB->BSRR = (1u << 8) | (1u << 9);   /* all lines high */
    mx_line = (uint8_t)((mx_line + 1) % 10);
    if (mx_line < 8) GPIOC->BRR = 1u << mx_line;
    else GPIOB->BRR = 1u << mx_line;                              /* PB8 / PB9 */
}

/* ------------------------------------------------------------------ ADC + 74HC4051 */
#define SEQ 8
#define NCH 5
static volatile uint16_t adc_buf[SEQ * NCH];
static volatile uint16_t slot[3][8];     /* [PA0 knobA, PA1 knobB, PA2 pads][mux addr] */
static volatile uint16_t joy[2];
static uint8_t mux_addr;
static const uint8_t KNOB_MUX[8] = { 3, 0, 1, 2, 5, 7, 6, 4 };   /* K1..K8 and pad1..8 -> 4051 input */

uint16_t knob_a(int k)   { return slot[0][KNOB_MUX[k & 7]]; }
uint16_t knob_b(int k)   { return slot[1][KNOB_MUX[k & 7]]; }
uint16_t pad_raw(int p)  { return slot[2][KNOB_MUX[p & 7]]; }
uint16_t joy_raw(int a)  { return joy[a & 1]; }
bool     pedal_down(void) { return !(GPIOA->IDR & (1u << 5)); }

static void adc_start_block(void)
{
    DMA1_Channel1->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF1;
    DMA1_Channel1->CNDTR = SEQ * NCH;
    DMA1_Channel1->CCR |= DMA_CCR_EN;
    ADC1->ISR = ADC_ISR_OVR;
    ADC1->CR |= ADC_CR_ADSTART;
}

static inline void adc_step(void)
{
    if (!(DMA1->ISR & DMA_ISR_TCIF1)) return;
    ADC1->CR |= ADC_CR_ADSTP;
    while (ADC1->CR & ADC_CR_ADSTP) { }
    uint8_t done = mux_addr;
    mux_addr = (uint8_t)((mux_addr + 1) & 7);
    GPIOB->ODR = (GPIOB->ODR & ~7u) | mux_addr;
    /* average sequences 1..7 (sequence 0 may still see the previous mux settling) */
    uint32_t acc[NCH] = { 0 };
    for (int s = 1; s < SEQ; s++) for (int c = 0; c < NCH; c++) acc[c] += adc_buf[s * NCH + c];
    adc_start_block();
    for (int c = 0; c < 3; c++) slot[c][done] = (uint16_t)(acc[c] / (SEQ - 1));
    if (done == 0) joy[0] = (uint16_t)(acc[3] / (SEQ - 1));   /* stock samples PA3 only at mux 0 */
    joy[1] = (uint16_t)(acc[4] / (SEQ - 1));
}

static void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADCEN;
    for (int p = 0; p <= 4; p++) pin_mode(GPIOA, p, ANALOG, NOPULL);
    if (ADC1->CR & ADC_CR_ADEN) { ADC1->CR |= ADC_CR_ADDIS; while (ADC1->CR & ADC_CR_ADEN) { } }
    ADC1->CFGR2 = 0;                                     /* async HSI14 clock */
    RCC->CR2 |= RCC_CR2_HSI14ON; while (!(RCC->CR2 & RCC_CR2_HSI14RDY)) { }
    ADC1->CR |= ADC_CR_ADCAL; while (ADC1->CR & ADC_CR_ADCAL) { }
    ADC1->CFGR1 = ADC_CFGR1_CONT | ADC_CFGR1_DMAEN;      /* 12-bit, right aligned, one-shot DMA */
    ADC1->SMPR = 2;                                       /* 13.5 cycles */
    ADC1->CHSELR = 0x1F;                                  /* ch0..4 */
    ADC1->ISR = ADC_ISR_ADRDY; ADC1->CR |= ADC_CR_ADEN; while (!(ADC1->ISR & ADC_ISR_ADRDY)) { }
    DMA1_Channel1->CPAR = (uint32_t)&ADC1->DR;
    DMA1_Channel1->CMAR = (uint32_t)adc_buf;
    DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_PSIZE_0 | DMA_CCR_MSIZE_0 | DMA_CCR_PL_1;
    adc_start_block();
}

/* ------------------------------------------------------------------ LEDs: 3x 74HC595 on SPI1, 32-level PWM */
static volatile uint8_t led_level[LED_COUNT];
static uint8_t led_frame, led_phase;
void led_set(int n, uint8_t level) { if (n >= 0 && n < LED_COUNT) led_level[n] = level > 32 ? 32 : level; }

static inline void led_step(void)
{
    if (led_phase == 0) {
        uint8_t b[3] = { 0xFF, 0xFF, 0xFF };              /* bit = 0 -> LED on */
        for (int n = 0; n < LED_COUNT; n++) if (led_frame < led_level[n]) b[n >> 3] &= (uint8_t)~(1u << (n & 7));
        LO(GPIOB, 4);
        for (int i = 0; i < 3; i++) { while (!(SPI1->SR & SPI_SR_TXE)) { } *(volatile uint8_t *)&SPI1->DR = b[i]; }
        led_phase = 1;
    } else {
        if (SPI1->SR & SPI_SR_BSY) return;
        HI(GPIOB, 4);                                     /* latch */
        led_frame = (uint8_t)((led_frame + 1) & 31);
        led_phase = 0;
    }
}

static void led_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    pin_af(GPIOB, 3, 0); pin_af(GPIOB, 5, 0);
    pin_mode(GPIOB, 4, OUT, PULLUP); HI(GPIOB, 4);
    pin_mode(GPIOB, 7, OUT, PULLUP); LO(GPIOB, 7);
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE | (3u << 3); /* /16 = 3 MHz */
    SPI1->CR2 = (7u << 8) | SPI_CR2_FRXTH;               /* 8-bit */
    SPI1->CR1 |= SPI_CR1_SPE;
}

/* ------------------------------------------------------------------ OLED on SPI2 + DMA1 ch5 */
uint8_t oled_fb[OLED_W * OLED_H / 8];
static uint8_t oled_state;   /* 0 idle, 1 first half sent, 2 second half sent */

static void oled_cmd(const uint8_t *c, int n)
{
    LO(GPIOB, 14); LO(GPIOB, 12);
    for (int i = 0; i < n; i++) { while (!(SPI2->SR & SPI_SR_TXE)) { } *(volatile uint8_t *)&SPI2->DR = c[i]; }
    while (SPI2->SR & SPI_SR_BSY) { }
    HI(GPIOB, 12);
}

static void oled_dma(void)
{
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    DMA1->IFCR = DMA_IFCR_CGIF5;
    DMA1_Channel5->CMAR = (uint32_t)oled_fb;
    DMA1_Channel5->CNDTR = sizeof oled_fb;
    DMA1_Channel5->CCR |= DMA_CCR_EN;
}

bool oled_busy(void)
{
    if (oled_state == 0) return false;
    if (!(DMA1->ISR & DMA_ISR_TCIF5) || (SPI2->SR & SPI_SR_BSY)) return true;
    if (oled_state == 1) {                /* send the same frame to pages 4..7 too (visible half unconfirmed) */
        oled_state = 2; oled_dma(); return true;
    }
    HI(GPIOB, 12);
    oled_state = 0;
    return false;
}

void oled_flush(void)
{
    if (oled_busy()) return;
    static const uint8_t win[] = { 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07 };
    oled_cmd(win, sizeof win);
    HI(GPIOB, 14); LO(GPIOB, 12);
    oled_state = 1; oled_dma();
}

static void delay_ms(uint32_t ms) { uint32_t t = g_ms; while (g_ms - t < ms) { } }

static void oled_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    pin_af(GPIOB, 13, 0); pin_af(GPIOB, 15, 0);
    pin_mode(GPIOB, 10, OUT, PULLUP); pin_mode(GPIOB, 12, OUT, PULLUP); pin_mode(GPIOB, 14, OUT, PULLUP);
    HI(GPIOB, 12);
    SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE | (2u << 3); /* /8 = 6 MHz */
    SPI2->CR2 = (7u << 8) | SPI_CR2_FRXTH | SPI_CR2_TXDMAEN;
    SPI2->CR1 |= SPI_CR1_SPE;
    DMA1_Channel5->CPAR = (uint32_t)&SPI2->DR;
    DMA1_Channel5->CCR = DMA_CCR_MINC | DMA_CCR_DIR;
    LO(GPIOB, 10); delay_ms(3); HI(GPIOB, 10); delay_ms(3);
    static const uint8_t init[] = {   /* identical to stock v1.26 */
        0xAE, 0x00, 0x10, 0x40, 0x81, 0xCF, 0xA6, 0xA4, 0xA8, 0x3F, 0xD3, 0x00, 0xD5, 0x80,
        0xD9, 0xF1, 0xDA, 0x02, 0xDB, 0x20, 0x20, 0x00, 0xA1, 0xC8, 0x8D, 0x14, 0xAF };
    oled_cmd(init, sizeof init);
    memset(oled_fb, 0, sizeof oled_fb);
    oled_flush(); while (oled_busy()) { }
}

/* ------------------------------------------------------------------ clock in on TE1 debug header
 * PA13 (SWDIO) = clock, PA14 (SWCLK) = reset. Only reconfigured away from SWD when enabled in the menu.
 * Default polarity assumes an NPN transistor input stage (pulse pulls the pin low). */
static volatile bool clk_en, clk_inv;
static uint8_t clk_prev, rst_prev;
static volatile uint32_t clk_last_ts;
#define CLKQ 8
static volatile uint32_t clkq[CLKQ]; static volatile uint8_t clk_head, clk_tail;
static volatile uint8_t rst_pending;

static volatile bool clkout_en;

/* PA13 = clock in (when enabled); PA14 = reset in (with clock in) or clock out (pin2_out). Unused pins stay SWD. */
void clockin_enable(bool on, bool transistor, bool pin2_out)
{
    static int8_t last_on = -1, last_tr = -1, last_p2 = -1;
    if (last_on == on && last_tr == transistor && last_p2 == pin2_out) return;
    last_on = on; last_tr = transistor; last_p2 = pin2_out;
    clk_en = false; clkout_en = false;
    /* transistor stage = open collector -> needs pull-up; direct (resistor + clamp) input -> pull-down */
    uint32_t pull = transistor ? PULLUP : 2;
    if (on) pin_mode(GPIOA, 13, IN, pull);
    else { pin_af(GPIOA, 13, 0); GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 26)) | (1u << 26); }      /* SWDIO */
    if (pin2_out) { LO(GPIOA, 14); pin_mode(GPIOA, 14, OUT, NOPULL); clkout_en = true; }
    else if (on) pin_mode(GPIOA, 14, IN, pull);
    else { pin_af(GPIOA, 14, 0); GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << 28)) | (2u << 28); }      /* SWCLK */
    clk_prev = rst_prev = 0; clk_head = clk_tail = 0; rst_pending = 0;
    clk_inv = transistor; clk_en = on;
}

void clockout_set(bool high) { if (clkout_en) { if (high) HI(GPIOA, 14); else LO(GPIOA, 14); } }
bool clockin_pop(uint32_t *ts)
{
    if (clk_tail == clk_head) return false;
    *ts = clkq[clk_tail]; clk_tail = (uint8_t)((clk_tail + 1) % CLKQ); return true;
}
bool clockin_reset_pop(void) { if (!rst_pending) return false; rst_pending = 0; return true; }

static inline void clockin_step(void)
{
    uint32_t idr = GPIOA->IDR;
    uint8_t c = (uint8_t)((idr >> 13) & 1), r = (uint8_t)((idr >> 14) & 1);
    if (clk_inv) { c ^= 1; r ^= 1; }              /* active = pin low */
    if (c && !clk_prev && g_ticks - clk_last_ts >= 50) {   /* >= 1 ms between pulses */
        clk_last_ts = g_ticks;
        uint8_t n = (uint8_t)((clk_head + 1) % CLKQ);
        if (n != clk_tail) { clkq[clk_head] = g_ticks; clk_head = n; }
    }
    if (!clkout_en && r && !rst_prev) rst_pending = 1;
    clk_prev = c; rst_prev = r;
}

/* Probe free pins: read each with pull-up then pull-down. 0 = floating/free, 1 = tied low, 2 = tied high, 3 = odd.
 * Inputs only, so harmless. SWD pins are restored afterwards unless clock-in owns them. */
static const struct { GPIO_TypeDef *g; uint8_t pin; } PROBE[] = {
    { GPIOA, 13 }, { GPIOA, 14 }, { GPIOA, 8 }, { GPIOA, 9 }, { GPIOA, 10 }, { GPIOA, 15 },
    { GPIOB, 6 }, { GPIOB, 11 }, { GPIOD, 2 }, { GPIOA, 6 }, { GPIOA, 7 } };
int board_probe_pins(uint8_t *out)
{
    RCC->AHBENR |= RCC_AHBENR_GPIODEN;
    int n = (int)(sizeof PROBE / sizeof PROBE[0]);
    for (int i = 0; i < n; i++) {
        GPIO_TypeDef *g = PROBE[i].g; int p = PROBE[i].pin;
        if (clkout_en && g == GPIOA && p == 14) { out[i] = 0; continue; }   /* driving clock out: don't touch */
        uint32_t moder = g->MODER, pupd = g->PUPDR;
        pin_mode(g, p, IN, PULLUP);   for (volatile int d = 0; d < 2000; d++) { }
        int up = (g->IDR >> p) & 1;
        pin_mode(g, p, IN, 2);        for (volatile int d = 0; d < 2000; d++) { }
        int dn = (g->IDR >> p) & 1;
        g->MODER = moder; g->PUPDR = pupd;           /* restore exactly (clock in / out keep working) */
        out[i] = (uint8_t)(up && !dn ? 0 : !up && !dn ? 1 : up && dn ? 2 : 3);
    }
    return n;
}

/* ------------------------------------------------------------------ 50 kHz tick */
void TIM3_IRQHandler(void)
{
    TIM3->SR = 0;
    g_ticks++;
    matrix_step();
    if (clk_en) clockin_step();
    static uint8_t div;
    if (++div == 5) { div = 0; led_step(); adc_step(); }
}

void board_init(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN | RCC_AHBENR_DMAEN;
    /* matrix: PC0..7 + PB8/9 drive (push-pull, idle high), PC8..15 sense with pull-ups */
    GPIOC->BSRR = 0x00FF; GPIOB->BSRR = (1u << 8) | (1u << 9);
    for (int p = 0; p < 8; p++) pin_mode(GPIOC, p, OUT, NOPULL);
    pin_mode(GPIOB, 8, OUT, NOPULL); pin_mode(GPIOB, 9, OUT, NOPULL);
    for (int p = 8; p < 16; p++) pin_mode(GPIOC, p, IN, PULLUP);
    for (int p = 0; p < 3; p++) pin_mode(GPIOB, p, OUT, NOPULL);   /* 4051 ADDR0..2 */
    pin_mode(GPIOA, 5, IN, PULLUP);                                /* sustain pedal */
    adc_init();
    led_init();
    oled_init();
    /* TIM3: 48 MHz / 960 = 50 kHz */
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    TIM3->PSC = 0; TIM3->ARR = 959; TIM3->DIER = TIM_DIER_UIE; TIM3->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM3_IRQn, 1);
    NVIC_EnableIRQ(TIM3_IRQn);
    delay_ms(1);
    HI(GPIOB, 7);                                                  /* LED power enable */
}
