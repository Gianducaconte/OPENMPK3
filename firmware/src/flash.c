/* OPENMPK3 — internal flash helpers. Callers only pass addresses inside 0x08015000-0x0801F7FF (patterns, settings). */
#include "flash.h"
#include "stm32f0.h"

static int guard(uint32_t addr) { return addr >= 0x08015000u && addr < 0x0801F800u; }

static void unlock(void)
{
    FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu;
    while (FLASH->SR & FLASH_SR_BSY) { }
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;
}

void flash_erase_page(uint32_t addr)
{
    if (!guard(addr)) return;
    __disable_irq(); unlock();
    FLASH->CR |= FLASH_CR_PER; FLASH->AR = addr; FLASH->CR |= FLASH_CR_STRT;
    while (FLASH->SR & FLASH_SR_BSY) { }
    FLASH->CR &= ~FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_LOCK; __enable_irq();
}

void flash_program(uint32_t addr, const void *data, uint32_t len)
{
    if (!guard(addr) || !guard(addr + len - 1)) return;
    const uint8_t *s = data;
    __disable_irq(); unlock();
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < len; i += 2) {
        uint16_t v = (uint16_t)(s[i] | ((i + 1 < len) ? s[i + 1] << 8 : 0xFF00));
        *(volatile uint16_t *)(addr + i) = v;
        while (FLASH->SR & FLASH_SR_BSY) { }
    }
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK; __enable_irq();
}
