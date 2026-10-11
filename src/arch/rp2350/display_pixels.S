.syntax unified
.cpu cortex-m33
.thumb
.section .text.badger_display_write_pixels_hw,"ax",%progbits
.balign 4
.global badger_display_write_pixels_hw
.type badger_display_write_pixels_hw,%function
.thumb_func
badger_display_write_pixels_hw:
cmp r1, #0
beq .Ldisplay_empty
push {r4, r5, r6, r7, lr}
mov r4, r2
movs r5, #255
movs r7, #1
lsls r7, #30
.Ldisplay_pixel:
ldrh r6, [r0]
lsrs r3, r6, #8
str r5, [r4, #36]
str r3, [r4, #28]
str r7, [r4, #32]
nop
nop
nop
str r7, [r4, #24]
nop
nop
lsls r3, r6, #24
lsrs r3, #24
str r5, [r4, #36]
str r3, [r4, #28]
str r7, [r4, #32]
nop
nop
nop
str r7, [r4, #24]
nop
nop
adds r0, #2
subs r1, #1
bne .Ldisplay_pixel
pop {r4, r5, r6, r7, pc}
.Ldisplay_empty:
bx lr
.size badger_display_write_pixels_hw, .-badger_display_write_pixels_hw
.section .note.GNU-stack,"",%progbits
