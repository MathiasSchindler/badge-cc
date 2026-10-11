.syntax unified
.cpu cortex-m33
.thumb
.eabi_attribute 18, 4
.eabi_attribute 24, 1
.eabi_attribute 25, 1
.eabi_attribute 26, 2
.eabi_attribute 28, 0

.section .text.entry,"ax",%progbits
.p2align 2
.global entry
.type entry,%function
.thumb_func
entry:
    bl callee
    b.w callee
    bx lr
.size entry, .-entry

.section .text.callee,"ax",%progbits
.p2align 2
.global callee
.hidden callee
.type callee,%function
.thumb_func
callee:
    .fnstart
    .cantunwind
    movs r0, #7
    bx lr
.fnend
.size callee, .-callee

.section .data.refs,"aw",%progbits
.p2align 2
.global refs
.type refs,%object
refs:
    .word callee
    .word callee - .
    .word weak_missing
    .word common_data
.size refs, .-refs
.weak weak_missing
.comm common_data, 8, 8

.section .bss.zero,"aw",%nobits
.p2align 3
.global zero
.type zero,%object
zero:
    .space 16
.size zero, .-zero

.addrsig
.addrsig_sym callee
.addrsig_sym refs
