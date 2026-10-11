.syntax unified
.cpu cortex-m33
.thumb
.section .text.wifi_pio_write_words,"ax",%progbits
.balign 4
.global wifi_pio_write_words
.type wifi_pio_write_words,%function
.thumb_func
wifi_pio_write_words:
cmp r2, #0
beq .Ltx_empty
push {r4, r5, r6, r7, lr}
mov r7, r3
ldr r4, [r0]
ldr r5, [r0, #4]
ldr r6, [r0, #12]
movw r12, #34464
movt r12, #1
.Ltx_word:
ldr r3, [r6]
subs r3, r3, r7
cmp r3, r12
bhs .Ltx_timeout
movw r0, #16960
movt r0, #15
.Ltx_wait:
ldr r3, [r4]
lsls r3, r3, #15
bpl .Ltx_ready
ldr r3, [r6]
subs r3, r3, r7
cmp r3, r12
bhs .Ltx_timeout
subs r0, #1
bne .Ltx_wait
b .Ltx_timeout
.Ltx_ready:
ldrb r3, [r1]
lsls r3, #8
ldrb r0, [r1, #1]
orrs r3, r0
lsls r3, #8
ldrb r0, [r1, #2]
orrs r3, r0
lsls r3, #8
ldrb r0, [r1, #3]
orrs r3, r0
str r3, [r5]
adds r1, #4
subs r2, #1
bne .Ltx_word
movs r0, #1
pop {r4, r5, r6, r7, pc}
.Ltx_timeout:
movs r0, #0
pop {r4, r5, r6, r7, pc}
.Ltx_empty:
movs r0, #1
bx lr
.size wifi_pio_write_words, .-wifi_pio_write_words

.section .text.wifi_pio_read_words,"ax",%progbits
.balign 4
.global wifi_pio_read_words
.type wifi_pio_read_words,%function
.thumb_func
wifi_pio_read_words:
cmp r2, #0
beq .Lrx_empty
push {r4, r5, r6, r7, lr}
mov r7, r3
ldr r4, [r0]
ldr r5, [r0, #8]
ldr r6, [r0, #12]
movw r12, #34464
movt r12, #1
.Lrx_word:
ldr r3, [r6]
subs r3, r3, r7
cmp r3, r12
bhs .Lrx_timeout
movw r0, #16960
movt r0, #15
.Lrx_wait:
ldr r3, [r4]
lsls r3, r3, #23
bpl .Lrx_ready
ldr r3, [r6]
subs r3, r3, r7
cmp r3, r12
bhs .Lrx_timeout
subs r0, #1
bne .Lrx_wait
b .Lrx_timeout
.Lrx_ready:
ldr r3, [r5]
strb r3, [r1, #3]
lsrs r3, #8
strb r3, [r1, #2]
lsrs r3, #8
strb r3, [r1, #1]
lsrs r3, #8
strb r3, [r1]
adds r1, #4
subs r2, #1
bne .Lrx_word
movs r0, #1
pop {r4, r5, r6, r7, pc}
.Lrx_timeout:
movs r0, #0
pop {r4, r5, r6, r7, pc}
.Lrx_empty:
movs r0, #1
bx lr
.size wifi_pio_read_words, .-wifi_pio_read_words
.section .text.wifi_pio_prepare_hw,"ax",%progbits
.balign 4
.global wifi_pio_prepare_hw
.type wifi_pio_prepare_hw,%function
.thumb_func
wifi_pio_prepare_hw:
push {r4, lr}
movw r4, #0x6001
str r4, [r0, #72]
movw r4, #0x1040
str r4, [r0, #76]
movw r4, #0xe080
str r4, [r0, #80]
cmp r3, #0
beq .Lprepare_write
movw r4, #0x5001
str r4, [r0, #84]
movw r4, #0x0083
str r4, [r0, #88]
b .Lprepare_common
.Lprepare_write:
movw r4, #0xc000
str r4, [r0, #84]
movs r4, #4
str r4, [r0, #88]
.Lprepare_common:
movw r4, #0xc000
str r4, [r0, #92]
movs r4, #6
str r4, [r0, #96]
movs r4, #8
lsls r4, #16
str r4, [r0, #200]
movw r4, #0x6000
str r4, [r0, #204]
movs r4, #0
str r4, [r0, #208]
movw r4, #0x03a0
movt r4, #0x0400
str r4, [r0, #220]
movw r4, #0xe000
str r4, [r0, #216]
movw r4, #0xe081
str r4, [r0, #216]
movs r4, #6
str r4, [r1, #236]
movw r4, #0x7718
movt r4, #0x241c
str r4, [r0, #220]
movw r4, #0xe000
str r4, [r0, #216]
lsls r4, r2, #3
subs r4, #1
str r4, [r0, #16]
movw r4, #0x80a0
str r4, [r0, #216]
movw r4, #0x6020
str r4, [r0, #216]
movs r4, #0
cmp r3, #0
beq .Lprepare_rx_count
lsls r4, r3, #3
subs r4, #1
.Lprepare_rx_count:
str r4, [r0, #16]
movw r4, #0x80a0
str r4, [r0, #216]
movw r4, #0x6040
str r4, [r0, #216]
movs r4, #3
lsls r4, #16
str r4, [r0, #208]
movs r4, #1
lsls r4, #24
str r4, [r0, #56]
movs r4, #0
str r4, [r0, #216]
pop {r4, pc}
.size wifi_pio_prepare_hw, .-wifi_pio_prepare_hw

.section .text.wifi_pio_finish_hw,"ax",%progbits
.balign 4
.global wifi_pio_finish_hw
.type wifi_pio_finish_hw,%function
.thumb_func
wifi_pio_finish_hw:
movs r3, #0
str r3, [r0]
movw r3, #0xe080
str r3, [r0, #216]
movs r3, #1
lsls r3, #24
str r3, [r1, #64]
movs r3, #1
lsls r3, #29
str r3, [r1, #32]
movs r3, #5
str r3, [r2, #196]
str r3, [r2, #236]
movs r3, #1
lsls r3, #29
str r3, [r1, #32]
movs r3, #1
lsls r3, #25
str r3, [r1, #24]
movs r3, #1
lsls r3, #24
str r3, [r1, #64]
bx lr
.size wifi_pio_finish_hw, .-wifi_pio_finish_hw
.section .note.GNU-stack,"",%progbits
