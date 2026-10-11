.syntax unified
.cpu cortex-m33
.thumb
.section .text.ba03_probe,"ax",%progbits
.global ba03_probe
.type ba03_probe,%function
.thumb_func
ba03_probe:
    push.w {r4-r11, lr}
    sub sp, sp, #4
    mrs r4, primask
    mrs r5, basepri
    mrs r6, faultmask
    mrs r7, control
    mrs r8, apsr
    movw r10, #:lower16:ba03_progress
    movt r10, #:upper16:ba03_progress
    movs r0, #0
    msr primask, r0
    msr basepri, r0
    msr faultmask, r0
    mov.w r11, #1
    str r11, [r10]
    cpsid i
    mrs r0, primask
    cmp r0, #1
    bne .Lfailed
    cpsie i
    mrs r0, primask
    cmp r0, #0
    bne .Lfailed
    mov.w r11, #2
    str r11, [r10]
    movs r0, #128
    msr basepri, r0
    mrs r1, basepri
    cmp r1, r0
    bne .Lfailed
    mrs r1, basepri_max
    cmp r1, r0
    bne .Lfailed
    movs r0, #192
    msr basepri_max, r0
    mrs r1, basepri
    cmp r1, #128
    bne .Lfailed
    movs r0, #64
    msr basepri_max, r0
    mrs r1, basepri
    cmp r1, r0
    bne .Lfailed
    movs r0, #0
    msr basepri, r0
    mov.w r11, #3
    str r11, [r10]
    cpsid f
    mrs r0, faultmask
    cmp r0, #1
    bne .Lfailed
    cpsie f
    mrs r0, faultmask
    cmp r0, #0
    bne .Lfailed
    cpsid if
    mrs r0, primask
    mrs r1, faultmask
    cmp r0, #1
    bne .Lfailed
    cmp r1, #1
    bne .Lfailed
    cpsie if
    mov.w r11, #4
    str r11, [r10]
    mov.w r0, #0xf8000000
    msr apsr_nzcvq, r0
    mrs r1, apsr
    and.w r1, r1, #0xf8000000
    cmp r1, r0
    bne .Lfailed
    mov.w r0, #0x000f0000
    msr apsr_g, r0
    mrs r1, apsr
    and.w r1, r1, #0x000f0000
    cmp r1, r0
    bne .Lfailed
    mov.w r11, #5
    str r11, [r10]
    mrs r0, msp
    msr msp, r0
    mrs r1, msp
    cmp r1, r0
    bne .Lfailed
    mrs r0, psp
    msr psp, r0
    mrs r1, psp
    cmp r1, r0
    bne .Lfailed
    mrs r0, msplim
    msr msplim, r0
    mrs r1, msplim
    cmp r1, r0
    bne .Lfailed
    mrs r0, psplim
    msr psplim, r0
    mrs r1, psplim
    cmp r1, r0
    bne .Lfailed
    msr control, r7
    isb sy
    mrs r0, control
    cmp r0, r7
    bne .Lfailed
    mov.w r11, #6
    str r11, [r10]
    movw r9, #:lower16:ba03_exception
    movt r9, #:upper16:ba03_exception
    movs r0, #0
    str r0, [r9]
    svc #37
    ldr r0, [r9]
    cmp r0, #11
    bne .Lfailed
    mov.w r11, #0
.Lfailed:
    str r11, [r10]
    msr basepri, r5
    msr faultmask, r6
    msr primask, r4
    dsb sy
    isb sy
    mov r0, r11
    msr apsr_nzcvqg, r8
    add sp, sp, #4
    pop.w {r4-r11, pc}
.size ba03_probe, .-ba03_probe

.section .text.ba03_svc,"ax",%progbits
.global ba03_svc
.type ba03_svc,%function
.thumb_func
ba03_svc:
    mrs r0, ipsr
    movw r1, #:lower16:ba03_exception
    movt r1, #:upper16:ba03_exception
    str r0, [r1]
    bx lr
.size ba03_svc, .-ba03_svc
