.syntax unified
.cpu cortex-m33
.thumb

.section .vectors, "a"
.p2align 2
.word __StackTop
.word bare_reset
.word red_square_halt, red_square_halt, red_square_halt
.word red_square_halt, red_square_halt
.word 0, 0, 0, 0
.word red_square_halt, red_square_halt, 0, red_square_halt, red_square_halt

.section .text.bare_reset, "ax"
.global bare_reset
.thumb_func
bare_reset:
    /* cpsid i; encoding also supported by the owned assembler */
    .hword 0xb672
    movw r0, #0xed08
    movt r0, #0xe000
    movs r1, #1
    lsls r1, r1, #28
    str r1, [r0]
    bl bare_main
.thumb_func
red_square_halt:
    wfi
    b red_square_halt
