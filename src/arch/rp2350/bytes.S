.syntax unified
.cpu cortex-m33
.thumb
.section .text.badger_bytes_copy,"ax",%progbits
.balign 4
.global badger_bytes_copy
.type badger_bytes_copy,%function
.thumb_func
badger_bytes_copy:
cmp r2, #0
beq .Lcopy_end
.Lcopy_loop:
ldrb r3, [r1]
strb r3, [r0]
adds r0, #1
adds r1, #1
subs r2, #1
bne .Lcopy_loop
.Lcopy_end:
bx lr
.size badger_bytes_copy, .-badger_bytes_copy

.section .text.badger_bytes_zero,"ax",%progbits
.balign 4
.global badger_bytes_zero
.type badger_bytes_zero,%function
.thumb_func
badger_bytes_zero:
cmp r1, #0
beq .Lzero_end
movs r2, #0
.Lzero_loop:
strb r2, [r0]
adds r0, #1
subs r1, #1
bne .Lzero_loop
.Lzero_end:
bx lr
.size badger_bytes_zero, .-badger_bytes_zero

.section .text.badger_bytes_equal,"ax",%progbits
.balign 4
.global badger_bytes_equal
.type badger_bytes_equal,%function
.thumb_func
badger_bytes_equal:
cmp r2, #0
beq .Lequal_end
.Lequal_loop:
ldrb r3, [r0]
ldrb.w r12, [r1]
cmp r3, r12
bne .Lnot_equal
adds r0, #1
adds r1, #1
subs r2, #1
bne .Lequal_loop
.Lequal_end:
movs r0, #1
bx lr
.Lnot_equal:
movs r0, #0
bx lr
.size badger_bytes_equal, .-badger_bytes_equal

.section .text.badger_pixels_fill,"ax",%progbits
.balign 4
.global badger_pixels_fill
.type badger_pixels_fill,%function
.thumb_func
badger_pixels_fill:
cmp r2, #0
beq .Lfill_end
.Lfill_loop:
strh r1, [r0]
adds r0, #2
subs r2, #1
bne .Lfill_loop
.Lfill_end:
bx lr
.size badger_pixels_fill, .-badger_pixels_fill
.section .text.badger_bytes_load_u32,"ax",%progbits
.balign 4
.global badger_bytes_load_u32
.type badger_bytes_load_u32,%function
.thumb_func
badger_bytes_load_u32:
ldrb r3, [r0, #3]
lsls r3, #8
ldrb r1, [r0, #2]
orrs r3, r1
lsls r3, #8
ldrb r1, [r0, #1]
orrs r3, r1
lsls r3, #8
ldrb r1, [r0]
orrs r3, r1
mov r0, r3
bx lr
.size badger_bytes_load_u32, .-badger_bytes_load_u32

.section .text.badger_bytes_store_u32,"ax",%progbits
.balign 4
.global badger_bytes_store_u32
.type badger_bytes_store_u32,%function
.thumb_func
badger_bytes_store_u32:
strb r1, [r0]
lsrs r1, #8
strb r1, [r0, #1]
lsrs r1, #8
strb r1, [r0, #2]
lsrs r1, #8
strb r1, [r0, #3]
bx lr
.size badger_bytes_store_u32, .-badger_bytes_store_u32
.section .note.GNU-stack,"",%progbits
