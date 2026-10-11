/* Isolated hardware test: reserve 64 KiB of main SRAM below the mailbox. */
MEMORY
{
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4096k
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 444k
    STACK(rw) : ORIGIN = 0x2006f000, LENGTH = 64k
    MAILBOX(rw) : ORIGIN = 0x2007f000, LENGTH = 4k
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

    .noinit (NOLOAD) : {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    .report_mailbox (NOLOAD) : {
        __report_request = .;
        . += 0x100;
        __report_response = .;
        . += 0x100;
    } > MAILBOX

    .flash_end : {
        KEEP(*(.embedded_end_block))
        . = ALIGN(4);
        __flash_binary_end = .;
    } > FLASH =0xaa

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }

    __StackBottom = ORIGIN(STACK);
    __StackTop = ORIGIN(STACK) + LENGTH(STACK);
    PROVIDE(__stack = __StackTop);
    ASSERT(__report_request == 0x2007f000, "request mailbox address changed")
    ASSERT(__report_response == 0x2007f100, "response mailbox address changed")
    ASSERT(__flash_binary_end <= ORIGIN(FLASH) + LENGTH(FLASH),
        "firmware exceeds flash build limit")
}
