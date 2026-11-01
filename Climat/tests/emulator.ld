ENTRY(auto_selftest)
MEMORY { FLASH (rx) : ORIGIN = 0x10000, LENGTH = 1M
 RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 128K }
SECTIONS {
 .text : { *(.text*) *(.rodata*) } > FLASH
 .ARM.exidx : { *(.ARM.exidx*) } > FLASH
 .data : { *(.data*) } > RAM
 .bss (NOLOAD) : { *(.bss*) *(COMMON) . = ALIGN(8); end = .; _end = .; } > RAM
}
