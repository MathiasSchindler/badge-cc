.syntax unified
.cpu cortex-m33
.thumb
.section .vectors,"a",%progbits
@ Use mapped MPS2 RAM so the 64 KiB frame tests fit above globals.
.word 0x80100000
.word entry
.section .text.entry,"ax",%progbits
.global entry
.type entry,%function
.thumb_func
entry:
bl main
sub sp, #8
str r0, [sp, #4]
movw r0, #38
movt r0, #2
str r0, [sp]
mov r1, sp
movs r0, #32
.short 0xbeab
b .
.size entry, .-entry
.section .text.__aeabi_memclr4,"ax",%progbits
.global __aeabi_memclr4
.type __aeabi_memclr4,%function
.thumb_func
__aeabi_memclr4:
movs r2, #0
.global __aeabi_memset4
.type __aeabi_memset4,%function
.thumb_func
__aeabi_memset4:
.Lclear_loop:
cmp r1, #0
beq .Lclear_end
strb r2, [r0]
adds r0, #1
subs r1, #1
b .Lclear_loop
.Lclear_end:
bx lr
.size __aeabi_memclr4, .-__aeabi_memclr4
.size __aeabi_memset4, .-__aeabi_memset4
.section .text.__aeabi_memcpy4,"ax",%progbits
.global __aeabi_memcpy4
.type __aeabi_memcpy4,%function
.thumb_func
__aeabi_memcpy4:
.global __aeabi_memcpy
.type __aeabi_memcpy,%function
.thumb_func
__aeabi_memcpy:
.Lcopy_loop:
cmp r2, #0
beq .Lcopy_end
ldrb r3, [r1]
strb r3, [r0]
adds r0, #1
adds r1, #1
subs r2, #1
b .Lcopy_loop
.Lcopy_end:
bx lr
.size __aeabi_memcpy4, .-__aeabi_memcpy4
.size __aeabi_memcpy, .-__aeabi_memcpy
