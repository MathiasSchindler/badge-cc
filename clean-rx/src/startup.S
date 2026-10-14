.syntax unified
.cpu cortex-m3
.thumb

.section .vectors,"a",%progbits
.balign 256
.global cyw43_vectors
cyw43_vectors:
    .word 0x0001f000
    .word cyw43_reset
    .rept 62
    .word cyw43_fault
    .endr

.section .text.cyw43_reset,"ax",%progbits
.global cyw43_reset
.type cyw43_reset,%function
.thumb_func
cyw43_reset:
    cpsid i
    ldr r0, =0x0001f000
    msr msp, r0
    ldr r1, =0xe000ed08
    movs r0, #0
    str r0, [r1]
    dsb
    isb
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    movs r2, #0
1:
    cmp r0, r1
    bhs 2f
    str r2, [r0], #4
    b 1b
2:
    bl cyw43_main
3:
    wfi
    b 3b
.size cyw43_reset, .-cyw43_reset
.ltorg

.section .text.cyw43_fault,"ax",%progbits
.global cyw43_fault
.type cyw43_fault,%function
.thumb_func
cyw43_fault:
    cpsid i
    ldr r0, =0x00020000
    movs r1, #0xff
    str r1, [r0, #4]
    mrs r1, ipsr
    str r1, [r0, #12]
    ldr r2, =0xe000ed28
    ldr r1, [r2]
    str r1, [r0, #16]
    ldr r1, [r2, #4]
    str r1, [r0, #20]
    dsb
4:
    wfi
    b 4b
.size cyw43_fault, .-cyw43_fault
.ltorg
