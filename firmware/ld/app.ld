/* OPENMPK3 application linker script — STM32F070RB, runs behind Akai's 32 KiB bootloader area.
 * 0x08000000-0x08007FFF  Akai bootloader (never touched, copied verbatim into the image)
 * 0x08008000-0x08014FFF  OPENMPK3 application (52 KiB)
 * 0x08015000-0x0801DFFF  8 pattern slots + 1 undo slot, 4 KiB each
 * 0x0801E800-0x0801F7FF  settings store (2 x 2 KiB pages)
 * 0x0801F800-0x0801FFFF  last page: image tail (VID/PID/version/checksum at 0x0801FFF0)
 * RAM 0x20000000-0x200000BF holds the vector table copy (Cortex-M0 has no VTOR -> SYSCFG remap). */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08008000, LENGTH = 0xD000
  RAM   (rwx) : ORIGIN = 0x200000C0, LENGTH = 16K - 0xC0
}
__patterns_start = 0x08015000;
__settings_start = 0x0801E800;
__settings_end   = 0x0801F800;
__vectors_ram    = 0x20000000;
_estack = ORIGIN(RAM) + LENGTH(RAM);
PROVIDE(end = _ebss);

ENTRY(Reset_Handler)
SECTIONS
{
  .isr_vector : { . = ALIGN(4); __vectors_start = .; KEEP(*(.isr_vector)); __vectors_end = .; } > FLASH
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) *(.ramfunc*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } > RAM
  ._stack (NOLOAD) : { . = ALIGN(8); . = . + 0x600; } > RAM
}
