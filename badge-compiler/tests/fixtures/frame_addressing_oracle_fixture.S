.syntax unified
.cpu cortex-m33
.thumb
.text
.global entry
.type entry, %function
.thumb_func
entry:
uxtb r0, r5
orr.w r0, r0, r0, lsl #8
orr.w r0, r0, r0, lsl #16
ldr.w r0, [r5], #4
str.w r0, [r4], #4
ldrb.w r0, [r5], #1
strb.w r0, [r4], #1
add.w r0, r0, #256
addw r0, r0, #4095
subw r0, r0, #4095
and.w r0, r0, #0x00ff00ff
orr.w r0, r0, #0x80000000
eor.w r0, r0, #0x55555555
lsl.w r0, r0, #31
lsr.w r0, r0, #1
asr.w r0, r0, #31
add.w r0, r0, r1, lsl #0
add.w r0, r0, r1, lsl #3
add.w r0, r0, r1, lsl #31
mla r0, r0, r2, r1
udiv r4, r0, r2
sdiv r4, r0, r2
mls r0, r4, r2, r0
push.w {r4-r8, r10, r11, lr}
mov r10, r8
mov r11, r10
mov r6, r11
mov r8, r6
str.w r10, [r7, #4092]
ldr.w r11, [r7, #4092]
pop.w {r4-r8, r10, r11, lr}
mov r8, r0
mov r2, r8
str.w r8, [r7, #0]
str.w r8, [r7, #4092]
movw r12, #4096
add.w r12, r7, r12
str.w r8, [r12]
ldr.w r0, [r7, #0]
str.w r5, [r7, #0]
ldr.w r12, [r7, #4]
str.w r0, [r7, #124]
ldr.w r5, [r7, #128]
str.w r5, [r7, #252]
ldr.w r0, [r7, #256]
str.w r0, [r7, #1020]
ldr.w r12, [r7, #1024]
ldr.w r0, [r7, #4088]
ldr.w r1, [r7, #4092]
str.w r0, [r7, #4088]
str.w r1, [r7, #4092]
ldr.w r12, [r7, #4092]
movw r12, #4095
add.w r12, r7, r12
ldr.w r0, [r12]
movw r12, #4096
add.w r12, r7, r12
str.w r5, [r12]
movw r12, #65532
add.w r12, r7, r12
ldr.w r12, [r12]
movw r12, #0
movt r12, #1
add.w r12, r7, r12
ldr.w r5, [r12]
bx lr
.size entry, .-entry
