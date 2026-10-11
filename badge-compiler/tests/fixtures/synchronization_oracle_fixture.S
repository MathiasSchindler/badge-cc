.syntax unified
.cpu cortex-m33
.arch armv8-m.main
.thumb
.text
.global entry
.type entry,%function
.thumb_func
entry:
    ldrex r0, [r1]
    ldrex lr, [sp, #1020]
    strex r2, r0, [r1, #4]
    strex r12, lr, [sp, #1020]
    ldrexb r3, [r4]
    ldrexh lr, [sp]
    strexb r5, r3, [r4]
    strexh r12, lr, [sp]
    clrex
    lda r0, [r1]
    ldab lr, [sp]
    ldah r3, [r4]
    stl r0, [r1]
    stlb lr, [sp]
    stlh r3, [r4]
    ldaex r0, [r1]
    ldaexb lr, [sp]
    ldaexh r3, [r4]
    stlex r2, r0, [r1]
    stlexb r12, lr, [sp]
    stlexh r5, r3, [r4]
    ldrex r0, [r0]
    strex r2, r1, [r1]
    it eq
    ldrexeq r0, [r1]
    it ne
    stlexne r2, r0, [r1]
    it eq
    clrexeq
    dmb sy
    dsb sy
    sev
    wfe
    bx lr
.size entry, .-entry
