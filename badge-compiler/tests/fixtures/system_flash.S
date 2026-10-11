.syntax unified
.cpu cortex-m33
.thumb
.section .data.ba03_flash,"ax",%progbits
.global ba03_flash_commit
.type ba03_flash_commit,%function
.thumb_func
ba03_flash_commit:
    push {r4-r7, lr}
    sub sp, sp, #4
    mov r4, r0
    mov r5, r1
    mrs r7, primask
    cpsid i
    ldr r0, [r4]
    blx r0
    ldr r0, [r4, #4]
    blx r0
    mov.w r0, #0x100
    movw r1, #0xf000
    movt r1, #0x10ff
    mov.w r2, #4096
    movs r3, #0
    ldr r6, [r4, #8]
    blx r6
    mov r6, r0
    cmp r0, #0
    bne .Lrestore_xip
    movw r0, #0x100
    movt r0, #1
    movw r1, #0xf000
    movt r1, #0x10ff
    mov.w r2, #256
    mov r3, r5
    ldr r6, [r4, #8]
    blx r6
    mov r6, r0
.Lrestore_xip:
    ldr r0, [r4, #12]
    blx r0
    ldr r0, [r4, #16]
    blx r0
    msr primask, r7
    dsb sy
    isb sy
    mov r0, r6
    add sp, sp, #4
    pop {r4-r7, pc}
.size ba03_flash_commit, .-ba03_flash_commit
