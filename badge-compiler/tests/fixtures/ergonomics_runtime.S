.syntax unified
.arch armv8-m.main
.thumb
.section .vectors,"a",%progbits
.p2align 8
.word __StackTop,ba04_reset,ba04_fault,ba04_fault
.word ba04_fault,ba04_fault,ba04_fault,ba04_fault
.word 0,0,0,ba04_fault,ba04_fault,0,ba04_fault,ba04_fault
.section .bss,"aw",%nobits
.p2align 2
ba04_storage:
    .space 4
.text
.global ba04_reset
.type ba04_reset,%function
.thumb_func
ba04_reset:
    ldr r0,=0x12345678
    movw r1,#0x5678
    movt r1,#0x1234
    cmp r0,r1
    bne 9f
    ldr r2,=ba04_storage
    str r0,[r2]
    ldr r3,[r2]
    cmp r3,r1
    bne 9f
    movs r4,#3
1:
    subs r4,#1
    bne 1b
    b 2f
    .pool
2:
    ldr.w r8,=0x89abcdef
    movw r1,#0xcdef
    movt r1,#0x89ab
    cmp r8,r1
    bne 9f
    ldr r2,=ba04_storage
    ldr r0,[r2]
    cmp r0,r3
    bne 9f
    movs r0,#0
    b 8f
    .ltorg
9:
    movs r0,#127
8:
    sub sp,sp,#8
    str r0,[sp,#4]
    movw r0,#0x26
    movt r0,#2
    str r0,[sp]
    mov r1,sp
    movs r0,#0x20
    bkpt #0xab
    b 8b
.size ba04_reset,.-ba04_reset
.type ba04_fault,%function
.thumb_func
ba04_fault:
    movs r0,#127
    b 8b
