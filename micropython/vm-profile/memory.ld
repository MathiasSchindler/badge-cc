MEMORY
{
    CODE(rx) : ORIGIN = 0x10000000, LENGTH = 1M
    RAM(rwx) : ORIGIN = 0x80000000, LENGTH = 1M
}

REGION_ALIAS("ISR", CODE)
REGION_ALIAS("TEXT", CODE)
REGION_ALIAS("DATA", RAM)
REGION_ALIAS("BSS", RAM)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_profile_stack_bottom = _estack - 16K;

INCLUDE "mcu/arm/cortex-m.ld"

ASSERT(_ebss <= _profile_stack_bottom, "profile stack overlaps data or heap")
