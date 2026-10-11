ENTRY(entry)
SECTIONS {
    .vectors 0x10000000 : { KEEP(*(.vectors)) }
    .text : { *(.text*) *(.measure_flash) *(.measure_ram) *(.rodata*) }
    .data 0x80000000 : { *(.data*) }
    .bss : { *(.bss*) }
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
