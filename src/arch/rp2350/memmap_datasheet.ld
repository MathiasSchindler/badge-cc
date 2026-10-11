MEMORY {
    FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 4096k
    RAM(rwx) : ORIGIN = 0x20000000, LENGTH = 248k
    BENCH(rw) : ORIGIN = 0x2003e000, LENGTH = 8k
    REMOTE(rw) : ORIGIN = 0x20040000, LENGTH = 4k
    MAILBOX(rw) : ORIGIN = 0x20078000, LENGTH = 32k
    STACK(rw) : ORIGIN = 0x20081000, LENGTH = 4k
}
ENTRY(bare_reset)
SECTIONS {
    .text : {
        KEEP(*(.vectors))
        KEEP(*(.embedded_block))
        *start.c.o(.text*)
        *(.measure_flash)
        . = ALIGN(4);
    } > FLASH
    .data : {
        . = ALIGN(4);
        __data_start = .;
        *(.measure_ram)
        *(.text*)
        *(.rodata*)
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
    .bench (NOLOAD) : { . += LENGTH(BENCH); } > BENCH
    .remote (NOLOAD) : { . += LENGTH(REMOTE); } > REMOTE
    .mailbox (NOLOAD) : {
        __d02_ram_request = .;
        . += 0x100;
        __d02_response = .;
        . += 0x7f00;
    } > MAILBOX
    .flash_end : {
        KEEP(*(.embedded_end_block))
        . = ALIGN(4);
        __flash_binary_end = .;
    } > FLASH =0xaa
    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
    __StackTop = ORIGIN(STACK) + LENGTH(STACK);
    __d02_request = 0x10fe0000;
    ASSERT(__d02_ram_request == 0x20078000, "D02 RAM staging")
    ASSERT(__d02_response == 0x20078100, "D02 response")
    ASSERT(__bss_end <= ORIGIN(BENCH), "D02 low SRAM budget")
}
