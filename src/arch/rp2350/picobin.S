.syntax unified
.cpu cortex-m33
.thumb

.equ PICOBIN_BLOCK_MARKER_START, 0xffffded3
.equ PICOBIN_BLOCK_MARKER_END,   0xab123579
.equ PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE, 0x42
.equ PICOBIN_BLOCK_ITEM_2BS_IGNORED,    0xfe
.equ PICOBIN_BLOCK_ITEM_2BS_LAST,       0xff
.equ PICOBIN_IMAGE_TYPE_RP2350_ARM_S,   0x1021

.section .embedded_block, "a"
.p2align 2
.global rp2350_picobin_block
rp2350_picobin_block:
    .word PICOBIN_BLOCK_MARKER_START
    .byte PICOBIN_BLOCK_ITEM_1BS_IMAGE_TYPE
    .byte 0x1
    .hword PICOBIN_IMAGE_TYPE_RP2350_ARM_S
    .byte PICOBIN_BLOCK_ITEM_2BS_LAST
    .hword (rp2350_picobin_block_end - rp2350_picobin_block - 16) / 4
    .byte 0
    .word rp2350_picobin_end_block - rp2350_picobin_block
    .word PICOBIN_BLOCK_MARKER_END
rp2350_picobin_block_end:

.section .embedded_end_block, "a"
.p2align 2
.global rp2350_picobin_end_block
rp2350_picobin_end_block:
    .word PICOBIN_BLOCK_MARKER_START
    .byte PICOBIN_BLOCK_ITEM_2BS_IGNORED
    .byte 0x1
    .hword 0
    .byte PICOBIN_BLOCK_ITEM_2BS_LAST
    .hword (rp2350_picobin_end_block_end - rp2350_picobin_end_block - 16) / 4
    .byte 0
    .word rp2350_picobin_block - rp2350_picobin_end_block
    .word PICOBIN_BLOCK_MARKER_END
rp2350_picobin_end_block_end:
