ENTRY(entry)
SECTIONS {
  .vectors 0x10000000 : { KEEP(*(.vectors)) }
  .text : { *(.text*) *(.rodata*) }
  .init_array : {
    __fixture_init_start = .;
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(.init_array))
    __fixture_init_end = .;
  }
  .data 0x30000000 : { *(.data*) }
  .bss : { *(.bss*) }
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) }
}
