.syntax unified
.cpu cortex-m33
.thumb

.section .text.bare_main, "ax"
.global bare_main
.type bare_main, %function
.thumb_func
bare_main:
    push {r4, lr}
    movs r4, #1
    lsls r4, r4, #29
    movs r1, #0
    str r1, [r4, #4]
    movw r0, #0x5678
    movt r0, #0x1234
    bl micro_kernel
    str r0, [r4]
    movw r2, #0x4e45
    movt r2, #0x444f
    str r2, [r4, #4]
    pop {r4, pc}
.size bare_main, .-bare_main
