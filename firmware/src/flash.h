/* OPENMPK3 — internal flash helpers (2 KiB pages). Only our own data pages are ever touched. */
#pragma once
#include <stdint.h>
#define FLASH_PAGE 0x800u
void flash_erase_page(uint32_t addr);
void flash_program(uint32_t addr, const void *data, uint32_t len);   /* len rounded up to 2 bytes */
