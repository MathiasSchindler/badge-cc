.syntax unified
.cpu cortex-m33
.thumb
.section .vectors,"a",%progbits
.p2align 8
.global ba03_vectors
ba03_vectors:
    .word __StackTop, ba03_reset, ba03_fault, ba03_fault
    .word ba03_fault, ba03_fault, ba03_fault, ba03_fault
    .word 0, 0, 0, ba03_svc
    .word ba03_fault, 0, ba03_fault, ba03_fault
.section .bss,"aw",%nobits
.p2align 2
.global ba03_exception
ba03_exception:
    .space 4
.global ba03_progress
ba03_progress:
    .space 4
.section .text,"ax",%progbits
.global ba03_reset
.type ba03_reset,%function
.thumb_func
ba03_reset:
    movw r0, #:lower16:ba03_vectors
    movt r0, #:upper16:ba03_vectors
    movw r1, #0xed08
    movt r1, #0xe000
    str r0, [r1]
    dsb sy
    isb sy
    bl ba03_probe
    b ba03_exit
.type ba03_fault,%function
.thumb_func
ba03_fault:
    movs r0, #127
.type ba03_exit,%function
.thumb_func
ba03_exit:
    sub sp, sp, #8
    str r0, [sp, #4]
    movw r0, #0x26
    movt r0, #2
    str r0, [sp]
    mov r1, sp
    movs r0, #0x20
    bkpt #0xab
    b ba03_exit
