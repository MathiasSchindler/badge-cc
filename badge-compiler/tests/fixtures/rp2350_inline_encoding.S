.syntax unified
.thumb
.text
MRS r0, PRIMASK
BKPT #31
muls r0, r1
mcr p0, #3, r0, c0, c0
mcr p0, #5, r4, c7, c9, #6
mcrr p0, #4, r0, r1, c0
mcrr p0, #14, r4, r5, c7
mrc p0, #3, r6, c0, c0
mrrc p0, #3, r4, r5, c6
vstr d8, [r0, #48]
vstr d9, [r4, #-40]
vldr d10, [r0, #64]
vstr s31, [sp, #1020]
vldr s5, [r2, #-1020]
TT r0,r1
TTT r4,r6
TTA r5,r7
TTAT r10,r12
