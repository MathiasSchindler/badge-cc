MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4096k
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 512k
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
        __data_start = .;
        *(.data*)
        __data_end = .;
    } > RAM AT > FLASH
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss*) *(COMMON) *(.noinit*)
        __bss_end = .;
    } > RAM
    .flash_end : {
        KEEP(*(.embedded_end_block))
        . = ALIGN(4);
        __flash_binary_end = .;
    } > FLASH
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
    __StackTop = 0x20082000;
    ASSERT(__data_end == __data_start, "red square startup cannot initialize data")
    ASSERT(__bss_end == __bss_start, "red square startup cannot initialize BSS")
    ASSERT(__flash_binary_end <= ORIGIN(FLASH) + LENGTH(FLASH), "firmware exceeds physical flash")
}
