ENTRY(cyw43_reset)
MEMORY {
    CODE (rwx) : ORIGIN = 0x00000000, LENGTH = 0x00014000
    BSS  (rw)  : ORIGIN = 0x00014000, LENGTH = 0x00008000
}
SECTIONS {
    .vectors 0x00000000 : {
        KEEP(*(.vectors))
    } > CODE
    .text 0x00000100 : ALIGN(4) {
        KEEP(*(.text.cyw43_reset))
        KEEP(*(.text.cyw43_fault))
        *(.text*)
        *(.rodata*)
        *(.data*)
        . = ALIGN(4);
    } > CODE
    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > BSS
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment*)
        *(.note*)
    }
}
