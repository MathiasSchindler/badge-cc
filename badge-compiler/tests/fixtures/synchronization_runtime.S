.syntax unified
.cpu cortex-m33
.thumb
.section .vectors,"a",%progbits
.p2align 8
.word __StackTop,ba05_reset,ba05_fault,ba05_fault
.word ba05_fault,ba05_fault,ba05_fault,ba05_fault
.word 0,0,0,ba05_fault,ba05_fault,0,ba05_fault,ba05_fault
.section .bss,"aw",%nobits
.p2align 4
ba05_storage:
    .space 16
.text
.global ba05_reset
.type ba05_reset,%function
.thumb_func
ba05_reset:
    movw r4, #:lower16:ba05_storage
    movt r4, #:upper16:ba05_storage
    mov r0, r4
    bl ba05_basic_probe
    cmp r0, #0
    bne 9f
    movs r0, #0
    b 8f
9:
    movs r0, #127
8:
    sub sp, sp, #8
    str r0, [sp, #4]
    movw r0, #0x26
    movt r0, #2
    str r0, [sp]
    mov r1, sp
    movs r0, #0x20
    bkpt #0xab
    b 8b
.size ba05_reset, .-ba05_reset
.type ba05_fault,%function
.thumb_func
ba05_fault:
    movs r0, #126
    b 8b
