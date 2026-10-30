/* OPENMPK3 — startup for STM32F070RB running at 0x08008000 behind the Akai bootloader. */
#include <stdint.h>
#include <string.h>
#include "stm32f0.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack, __vectors_start, __vectors_end, __vectors_ram;
int main(void);

void Default_Handler(void) { for (;;) { } }
#define WEAK __attribute__((weak, alias("Default_Handler")))
void Reset_Handler(void);
WEAK void NMI_Handler(void);       WEAK void HardFault_Handler(void); WEAK void SVC_Handler(void);
WEAK void PendSV_Handler(void);    WEAK void SysTick_Handler(void);
WEAK void WWDG_IRQHandler(void);   WEAK void RTC_IRQHandler(void);    WEAK void FLASH_IRQHandler(void);
WEAK void RCC_IRQHandler(void);    WEAK void EXTI0_1_IRQHandler(void); WEAK void EXTI2_3_IRQHandler(void);
WEAK void EXTI4_15_IRQHandler(void); WEAK void DMA1_Ch1_IRQHandler(void); WEAK void DMA1_Ch2_3_IRQHandler(void);
WEAK void DMA1_Ch4_5_IRQHandler(void); WEAK void ADC1_IRQHandler(void); WEAK void TIM1_BRK_UP_IRQHandler(void);
WEAK void TIM1_CC_IRQHandler(void); WEAK void TIM3_IRQHandler(void);  WEAK void TIM6_IRQHandler(void);
WEAK void TIM7_IRQHandler(void);   WEAK void TIM14_IRQHandler(void);  WEAK void TIM15_IRQHandler(void);
WEAK void TIM16_IRQHandler(void);  WEAK void TIM17_IRQHandler(void);  WEAK void I2C1_IRQHandler(void);
WEAK void I2C2_IRQHandler(void);   WEAK void SPI1_IRQHandler(void);   WEAK void SPI2_IRQHandler(void);
WEAK void USART1_IRQHandler(void); WEAK void USART2_IRQHandler(void); WEAK void USART3_4_IRQHandler(void);
WEAK void USB_IRQHandler(void);

__attribute__((section(".isr_vector"), used))
void (*const vectors[48])(void) = {
    (void (*)(void))&_estack, Reset_Handler, NMI_Handler, HardFault_Handler, 0, 0, 0, 0, 0, 0, 0,
    SVC_Handler, 0, 0, PendSV_Handler, SysTick_Handler,
    WWDG_IRQHandler, 0, RTC_IRQHandler, FLASH_IRQHandler, RCC_IRQHandler, EXTI0_1_IRQHandler,
    EXTI2_3_IRQHandler, EXTI4_15_IRQHandler, 0, DMA1_Ch1_IRQHandler, DMA1_Ch2_3_IRQHandler,
    DMA1_Ch4_5_IRQHandler, ADC1_IRQHandler, TIM1_BRK_UP_IRQHandler, TIM1_CC_IRQHandler, 0,
    TIM3_IRQHandler, TIM6_IRQHandler, TIM7_IRQHandler, TIM14_IRQHandler, TIM15_IRQHandler,
    TIM16_IRQHandler, TIM17_IRQHandler, I2C1_IRQHandler, I2C2_IRQHandler, SPI1_IRQHandler,
    SPI2_IRQHandler, USART1_IRQHandler, USART2_IRQHandler, USART3_4_IRQHandler, 0, USB_IRQHandler,
};

/* The Akai bootloader jumps here WITHOUT reloading MSP, and its stack lives inside our .bss.
 * So load our own stack pointer before touching any memory. */
__attribute__((naked)) void Reset_Handler(void)
{
    __asm volatile ("cpsid i\n ldr r0, =_estack\n mov sp, r0\n b reset_c\n");
}

__attribute__((used, noreturn)) void reset_c(void)
{
    /* the bootloader leaves SysTick (and maybe USB) running: silence everything before we enable IRQs */
    SysTick->CTRL = 0;
    NVIC->ICER[0] = 0xFFFFFFFFu;
    NVIC->ICPR[0] = 0xFFFFFFFFu;
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
    /* vector table -> SRAM @0x20000000, then map SRAM at 0x00000000 */
    memcpy(&__vectors_ram, &__vectors_start, (size_t)((char *)&__vectors_end - (char *)&__vectors_start));
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGCOMPEN;
    SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~3u) | 3u;
    memcpy(&_sdata, &_sidata, (size_t)((char *)&_edata - (char *)&_sdata));
    memset(&_sbss, 0, (size_t)((char *)&_ebss - (char *)&_sbss));
    __asm volatile ("cpsie i");
    main();
    for (;;) { }
}
