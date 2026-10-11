.syntax unified
.arch armv8-m.main
.thumb
.text
.global entry
.type entry,%function
.thumb_func
entry:
    ldr r0, =0x12345678
    ldr r1, =0x12345678
    ldr.w r8, =data+4
    b 1f
    .ltorg
1:
    movs r2, #1
2:
    subs r2, #1
    bne 2b
    ldr r3, =0x89abcdef
    b 1f
    .pool
1:
    bx lr
.size entry, .-entry
.section .data,"aw",%progbits
.global data
data:
    .word 0, 0
