.syntax unified
.cpu cortex-m33
.thumb

.section .embedded_block, "a"
.p2align 2
.global rp2350_picobin_block
rp2350_picobin_block:
    .word 0xffffded3
    .byte 0x42
    .byte 1
    .hword 0x1021
    .byte 0xff
    .hword (rp2350_picobin_block_end - rp2350_picobin_block - 16) / 4
    .byte 0
    .word rp2350_picobin_end_block - rp2350_picobin_block
    .word 0xab123579
rp2350_picobin_block_end:

.section .embedded_end_block, "a"
.p2align 2
.global rp2350_picobin_end_block
rp2350_picobin_end_block:
    .word 0xffffded3
    .byte 0xfe
    .byte 1
    .hword 0
    .byte 0xff
    .hword (rp2350_picobin_end_block_end - rp2350_picobin_end_block - 16) / 4
    .byte 0
    .word rp2350_picobin_block - rp2350_picobin_end_block
    .word 0xab123579
rp2350_picobin_end_block_end:
