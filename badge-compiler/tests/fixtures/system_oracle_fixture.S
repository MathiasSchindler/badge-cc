.syntax unified
.cpu cortex-m33
.thumb
.section .text,"ax",%progbits
.global entry
.type entry,%function
.thumb_func
entry:
    mrs r0, apsr
    mrs r1, iapsr
    mrs r2, eapsr
    mrs r3, xpsr
    mrs r4, ipsr
    mrs r5, epsr
    mrs r6, iepsr
    mrs r7, msp
    mrs r8, psp
    mrs r9, msplim
    mrs r10, psplim
    mrs r11, primask
    mrs r12, basepri
    mrs r0, basepri_max
    mrs lr, faultmask
    mrs r0, control
    msr apsr_nzcvq, r0
    msr apsr_g, r1
    msr apsr_nzcvqg, r2
    msr msp, r3
    msr psp, r4
    msr msplim, r5
    msr psplim, r6
    msr primask, r7
    msr basepri, r8
    msr basepri_max, r9
    msr faultmask, r10
    msr control, lr
    mrs r0, msp_ns
    mrs r1, psp_ns
    mrs r2, msplim_ns
    mrs r3, psplim_ns
    mrs r4, primask_ns
    mrs r5, basepri_ns
    mrs r6, faultmask_ns
    mrs r7, control_ns
    mrs r8, sp_ns
    msr msp_ns, r0
    msr psp_ns, r1
    msr msplim_ns, r2
    msr psplim_ns, r3
    msr primask_ns, r4
    msr basepri_ns, r5
    msr faultmask_ns, r6
    msr control_ns, r7
    msr sp_ns, r8
    cpsid i
    cpsie i
    cpsid f
    cpsie f
    cpsid if
    cpsie if
    svc #0
    svc #255
    bkpt #0
    bkpt #255
    dmb
    dmb sy
    dsb
    dsb sy
    isb
    isb sy
    it eq
    mrseq r0, primask
    it ne
    msrne control, r1
    it eq
    svceq #1
    bx lr
.size entry, .-entry
