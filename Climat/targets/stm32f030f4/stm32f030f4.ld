ENTRY(Reset_Handler)
MEMORY {
  FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 15K
  CONFIG (r) : ORIGIN = 0x08003C00, LENGTH = 1K
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 4K
}
_estack = ORIGIN(RAM) + LENGTH(RAM);
_stack_limit = _estack - 2048;
__config_start__ = ORIGIN(CONFIG);
__config_end__ = ORIGIN(CONFIG) + LENGTH(CONFIG);
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text : { *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4); _sdata = .;
    *(.data*) *(.ramfunc*)
    . = ALIGN(4); _edata = .;
  } > RAM AT > FLASH
  .bss (NOLOAD) : {
    . = ALIGN(8); _sbss = .; *(.bss*) *(COMMON)
    . = ALIGN(8); _ebss = .; _end = .;
  } > RAM
  .stack _stack_limit (NOLOAD) : { . += 2048; } > RAM
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(LOADADDR(.data)+SIZEOF(.data)<=ORIGIN(CONFIG), "Firmware exceeds 15360 bytes")
ASSERT(_end<=_stack_limit, "Globals + 2048-byte stack exceed 4096-byte RAM")
