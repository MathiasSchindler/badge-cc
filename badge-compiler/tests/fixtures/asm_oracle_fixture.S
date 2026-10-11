.syntax unified
.cpu cortex-m33
.fpu fpv5-sp-d16
.thumb
.eabi_attribute 18, 4
.eabi_attribute 24, 1
.eabi_attribute 25, 1
.eabi_attribute 26, 2
.eabi_attribute 28, 0
.file "asm fixture.c"

.section .text.entry,"ax",%progbits
.p2align 2
.global entry
.type entry,%function
.thumb_func
entry:
.fnstart
movs r0, #255
mov r1, r8
mov.w r9, #0x55555555
movs.w r10, #0x80000000
movw r0, #0xabcd
movt r0, #0xfedc
adds r0, #255
adds r1, r0, #7
adds r2, r0, r1
add r0, r8
add r1, sp, #1020
add sp, #508
sub sp, #508
add.w r9, r8, r10, lsl #3
adds.w r0, r1, #0x10101010
sub.w r9, lr, r0, asr #31
subs.w r8, r0, #255
addw r0, r1, #4095
subw r8, r9, #4095
ands r0, r1
and.w r8, lr, r0, lsr #5
bics r0, r1
bic r1, r2, #0xff00ff00
orrs r0, r1
orr.w r8, r9, r10, lsl #31
orn r0, r1, r2
eors r0, r1
eor r9, r8, #1
adcs r0, r1
adc.w r8, r9, r10
sbcs r0, r1
sbc.w r8, r9, r10
mvns r0, r1
mvn.w r8, r9, lsr #2
rsbs r0, r1, #0
rsb r8, r9, #1000
cmp r0, #255
cmp r0, r8
cmp.w r9, r10, lsr #16
cmn.w r8, #304
tst r0, r1
tst.w r8, #0xff00
lsls r0, r1, #31
lsrs r0, r1, #32
asrs r0, r1, #32
lsls r0, r1
lsr.w r8, lr, r0
asr.w lr, r8, #31
rors r0, r1
ror.w r8, r9, #31
ldr r0, [r1, #124]
str r0, [sp, #1020]
ldrb r0, [r1, #31]
strh r0, [r1, #62]
ldrsb r0, [r1, r2]
ldrsh r0, [r1, r2]
ldr.w r8, [r9, #4095]
str.w r0, [r1, r2, lsl #3]
ldrb.w lr, [r8, #255]!
strh r0, [r1], #-255
ldrh r0, [r1, #-255]
ldrsh.w r8, [r9, #4095]
ldrd r0, r1, [sp, #1020]
strd r0, r1, [r8, #-1020]
push {r4-r7, lr}
pop {r4-r7, pc}
push.w {r4-r11, lr}
pop.w {r4-r11, lr}
ldm r0!, {r1, r2}
ldm r0, {r0, r1}
stm r0!, {r1, r2}
stm.w r8, {r0, r1}
ldm.w r8!, {r0, r1}
muls r0, r1, r0
mul lr, r8, r9
mla r0, r1, r2, r3
mls r8, r9, r10, lr
smlabb r0, r1, r2, r3
smull r0, r1, r8, lr
umull r0, r1, r8, lr
smlal r8, r9, r0, r1
sdiv r0, r1, r2
udiv lr, r8, r9
clz lr, r8
rev r0, r1
rev16.w r8, lr
uxtb r0, r1
uxth.w r8, r9
sxtb r0, r1
sxth.w lr, r8
uxtah r0, r1, r2
bfc r8, #0, #32
bfi r0, r1, #15, #17
ubfx r8, r9, #31, #1
ite eq
moveq r0, #255
movne r0, #1
itt ne
addne r0, r1, #7
subne r0, #255
it eq
addseq r0, r1, #7
it eq
movseq.w r8, r9
it eq
lslseq.w r8, r9, #3
dsb sy
dmb sy
isb sy
dsb
dmb
isb
nop
nop.w
wfi
yield
wfe
sev
cbz r0, .Lforward
cbnz r7, .Lforward
beq .Lforward
bne.w .Lforward
ldr r0, .Lpool
ldr.w r8, .Lpool
.Lforward:
adr.w r8, .Lpool
adr.w r0, entry
bl callee
b.w callee
bx lr
blx r8
tbb [pc, r0]
tbh [pc, r1, lsl #1]
.p2align 2
.Lpool:
.word callee
.word refs
.Lend:
.size entry, .Lend-entry
.cantunwind
.fnend

.section .text.callee,"ax",%progbits
.p2align 2
.global callee
.thumb_func
.type callee,%function
callee:
movs r0, #7
bx lr
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
.byte -128, 255
.short -32768, 65535
.long -2147483648, 4294967295
.ascii "quote\" slash\\ tab\t\000\377"
.asciz "hello"
.size refs, .-refs
.weak weak_missing
.comm common_data, 8, 8
.equ constant, 42
.equ location, .
.equ alias, refs+4
.global assignment_alias
assignment_alias = refs+8
.size assignment_alias, 4
.if constant != 42
.error "wrong constant"
.endif
.section .bss.zero,"aw",%nobits
.balign 8
.zero 16
.addrsig
.addrsig_sym refs
.addrsig_sym alias
.ident "badge assembler fixture"
.section .note.GNU-stack,"",%progbits
