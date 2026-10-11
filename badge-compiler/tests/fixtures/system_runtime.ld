ENTRY(ba03_reset)
MEMORY
{
    CODE(rx) : ORIGIN = 0x10000000, LENGTH = 64k
    RAM(rwx) : ORIGIN = 0x80000000, LENGTH = 1024k
}
SECTIONS
{
    .text : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > CODE
    .bss (NOLOAD) : { *(.bss*) } > RAM
    __StackTop = ORIGIN(RAM) + LENGTH(RAM);
}
