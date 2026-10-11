MEMORY {
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4096k
    RAM(rw) : ORIGIN = 0x20000000, LENGTH = 512k
}
ENTRY(entry)
SECTIONS {
    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    .data : {
        __data_start = .;
        *(.data*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM AT > FLASH
    __data_source = LOADADDR(.data);
    .bss (NOLOAD) : {
        __bss_start = .;
        KEEP(*(.bss.zero))
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > RAM
    .noinit (NOLOAD) : {
        *(.noinit*)
        . = ALIGN(4);
    } > RAM
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
    PROVIDE(stack = ORIGIN(RAM) + LENGTH(RAM));
    ASSERT(__data_start == 0x20000000, "data base")
    ASSERT(__data_end <= __bss_start, "data/bss overlap")
}
