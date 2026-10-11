.syntax unified
.cpu cortex-m33
.thumb
.text
.global ba05_lock
.type ba05_lock,%function
.thumb_func
ba05_lock:
.Lba05_lock:
    cmp r1, #0
    beq 4f
1:
    ldaex r2, [r0]
    cmp r2, #0
    bne 2f
    movs r2, #1
    stlex r3, r2, [r0]
    cmp r3, #0
    beq 3f
2:
    clrex
    subs r1, #1
    bne 1b
4:
    clrex
    movs r0, #1
    bx lr
3:
    movs r0, #0
    bx lr
.size ba05_lock, .-ba05_lock
.global ba05_unlock
.type ba05_unlock,%function
.thumb_func
ba05_unlock:
.Lba05_unlock:
    movs r1, #0
    stl r1, [r0]
    sev
    bx lr
.size ba05_unlock, .-ba05_unlock
.global ba05_acquire
.type ba05_acquire,%function
.thumb_func
ba05_acquire:
    lda r0, [r0]
    bx lr
.size ba05_acquire, .-ba05_acquire
.global ba05_release
.type ba05_release,%function
.thumb_func
ba05_release:
    stl r1, [r0]
    sev
    bx lr
.size ba05_release, .-ba05_release
.global ba05_reserve
.type ba05_reserve,%function
.thumb_func
ba05_reserve:
    ldrex r0, [r0]
    bx lr
.size ba05_reserve, .-ba05_reserve
.global ba05_commit
.type ba05_commit,%function
.thumb_func
ba05_commit:
    strex r2, r1, [r0]
    mov r0, r2
    bx lr
.size ba05_commit, .-ba05_commit
.global ba05_basic_probe
.type ba05_basic_probe,%function
.thumb_func
ba05_basic_probe:
    push {r4, lr}
    mov r4, r0
    movs r0, #0
    str r0, [r4]
    str r0, [r4, #4]
    ldrex r0, [r4]
    clrex
    movs r1, #42
    strex r2, r1, [r4]
    cmp r2, #0
    beq 9f
    ldr r0, [r4]
    cmp r0, #0
    bne 9f
    ldrex r0, [r4, #4]
    strex r2, r1, [r4, #4]
    cmp r2, #0
    bne 9f
    lda r0, [r4]
    stl r1, [r4]
    ldaex r0, [r4]
    cmp r0, #42
    bne 9f
    movs r1, #63
    stlex r2, r1, [r4]
    cmp r2, #0
    bne 9f
    lda r0, [r4]
    cmp r0, #63
    bne 9f
    movs r1, #0xab
    ldrexb r0, [r4]
    strexb r2, r1, [r4]
    cmp r2, #0
    bne 9f
    ldab r0, [r4]
    cmp r0, r1
    bne 9f
    stlb r1, [r4]
    ldaexb r0, [r4]
    movs r1, #0xcd
    stlexb r2, r1, [r4]
    cmp r2, #0
    bne 9f
    ldab r0, [r4]
    cmp r0, r1
    bne 9f
    movw r1, #0x1234
    ldrexh r0, [r4]
    strexh r2, r1, [r4]
    cmp r2, #0
    bne 9f
    ldah r0, [r4]
    cmp r0, r1
    bne 9f
    stlh r1, [r4]
    ldaexh r0, [r4]
    movw r1, #0x5678
    stlexh r2, r1, [r4]
    cmp r2, #0
    bne 9f
    ldah r0, [r4]
    cmp r0, r1
    bne 9f
    movs r0, #0
    str r0, [r4]
    mov r0, r4
    movs r1, #0
    bl .Lba05_lock
    cmp r0, #1
    bne 9f
    mov r0, r4
    movs r1, #10
    bl .Lba05_lock
    cmp r0, #0
    bne 9f
    mov r0, r4
    movs r1, #10
    bl .Lba05_lock
    cmp r0, #1
    bne 9f
    mov r0, r4
    bl .Lba05_unlock
    mov r0, r4
    movs r1, #10
    bl .Lba05_lock
    cmp r0, #0
    bne 9f
    mov r0, r4
    bl .Lba05_unlock
    movs r0, #0
    pop {r4, pc}
9:
    clrex
    movs r0, #1
    pop {r4, pc}
.size ba05_basic_probe, .-ba05_basic_probe
.global ba05_watchdog_wait
.type ba05_watchdog_wait,%function
.thumb_func
ba05_watchdog_wait:
1:
    wfe
    b 1b
.size ba05_watchdog_wait, .-ba05_watchdog_wait
.section .data.ba05_park,"awx",%progbits
.global ba05_park
.type ba05_park,%function
.thumb_func
ba05_park:
    cpsid i
    clrex
    stl r1, [r0]
    dsb sy
    sev
1:
    wfe
    b 1b
.size ba05_park, .-ba05_park
