/* OPENMPK3 — 8 MHz HSE -> PLL x6 = 48 MHz SYSCLK + USB clock; 1 kHz SysTick. */
#include "stm32f0.h"
#include "board.h"

volatile uint32_t g_ms;
uint32_t SystemCoreClock = 8000000;
void SysTick_Handler(void) { g_ms++; }

void clock_init(void)
{
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t t = 0; !(RCC->CR & RCC_CR_HSERDY) && t < 500000; t++) { }
    FLASH->ACR = FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY;            /* 1 wait state for 48 MHz */
    RCC->CFGR &= ~RCC_CFGR_SW;                                     /* back to HSI while we touch the PLL */
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) { }
    RCC->CR &= ~RCC_CR_PLLON;
    while (RCC->CR & RCC_CR_PLLRDY) { }
    RCC->CFGR2 = 0;                                                /* PREDIV /1 */
    RCC->CFGR = (RCC->CFGR & ~(RCC_CFGR_PLLMUL | RCC_CFGR_PLLSRC | RCC_CFGR_HPRE | RCC_CFGR_PPRE))
              | RCC_CFGR_PLLMUL6 | RCC_CFGR_PLLSRC_HSE_PREDIV;
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) { }
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) { }
    RCC->CFGR3 |= RCC_CFGR3_USBSW;                                 /* USB clock = PLL (48 MHz) */
    SystemCoreClock = 48000000;
    SysTick_Config(48000);
}
