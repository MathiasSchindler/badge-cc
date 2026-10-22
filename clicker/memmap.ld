MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4088k
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 512k
    SCRATCH_X(rwx) : ORIGIN = 0x20080000, LENGTH = 4k
    SCRATCH_Y(rwx) : ORIGIN = 0x20081000, LENGTH = 4k
}
ENTRY(bare_reset)
SECTIONS
{
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    .data : {
        . = ALIGN(4);
        __data_start = .;
        *(.data*)
        *(.ramfunc*)
        . = ALIGN(4);
        __data_end = .;
    } > RAM AT > FLASH
    __data_source = LOADADDR(.data);
    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > RAM
    .flash_end : {
        KEEP(*(.embedded_end_block))
        . = ALIGN(4);
        __flash_binary_end = .;
    } > FLASH =0xaa
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
    __StackTop = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    ASSERT(__flash_binary_end <= 0x103fe000, "firmware overlaps reserved bond journal")
}
