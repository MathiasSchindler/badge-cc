target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

define i32 @rotate(i64 %limit) {
entry:
  %zero = icmp eq i64 %limit, 0
  br i1 %zero, label %exit, label %loop
loop:
  %a = phi i32 [1, %entry], [%b, %loop]
  %b = phi i32 [2, %entry], [%c, %loop]
  %c = phi i32 [3, %entry], [%a, %loop]
  %ticks = phi i64 [0, %entry], [%next, %loop]
  %next = add i64 %ticks, 1
  %more = icmp ult i64 %next, %limit
  br i1 %more, label %loop, label %exit
exit:
  %x = phi i32 [1, %entry], [%a, %loop]
  %y = phi i32 [2, %entry], [%b, %loop]
  %z = phi i32 [3, %entry], [%c, %loop]
  %ab = mul i32 %x, 100
  %bc = mul i32 %y, 10
  %sum = add i32 %ab, %bc
  %result = add i32 %sum, %z
  ret i32 %result
}

define i32 @rotate_five(i16 %limit) {
entry:
  %zero = icmp eq i16 %limit, 0
  br i1 %zero, label %exit, label %loop
loop:
  %a = phi i32 [1, %entry], [%b, %loop]
  %b = phi i32 [2, %entry], [%c, %loop]
  %c = phi i32 [3, %entry], [%d, %loop]
  %d = phi i32 [4, %entry], [%e, %loop]
  %e = phi i32 [5, %entry], [%a, %loop]
  %ticks = phi i16 [0, %entry], [%next, %loop]
  %next = add i16 %ticks, 1
  %more = icmp ult i16 %next, %limit
  br i1 %more, label %loop, label %exit
exit:
  %x0 = phi i32 [1, %entry], [%a, %loop]
  %x1 = phi i32 [2, %entry], [%b, %loop]
  %x2 = phi i32 [3, %entry], [%c, %loop]
  %x3 = phi i32 [4, %entry], [%d, %loop]
  %x4 = phi i32 [5, %entry], [%e, %loop]
  %m0 = mul i32 %x0, 10000
  %m1 = mul i32 %x1, 1000
  %m2 = mul i32 %x2, 100
  %m3 = mul i32 %x3, 10
  %s0 = add i32 %m0, %m1
  %s1 = add i32 %m2, %m3
  %s2 = add i32 %s0, %s1
  %result = add i32 %s2, %x4
  ret i32 %result
}

define i32 @narrow_cycle(i16 %limit) {
entry:
  %zero = icmp eq i16 %limit, 0
  br i1 %zero, label %exit, label %loop
loop:
  %a = phi i8 [-1, %entry], [%b, %loop]
  %b = phi i8 [-128, %entry], [%a, %loop]
  %flag = phi i1 [true, %entry], [%invert, %loop]
  %ticks = phi i16 [0, %entry], [%next, %loop]
  %invert = xor i1 %flag, true
  %next = add i16 %ticks, 1
  %more = icmp ult i16 %next, %limit
  br i1 %more, label %loop, label %exit
exit:
  %x = phi i8 [-1, %entry], [%a, %loop]
  %y = phi i8 [-128, %entry], [%b, %loop]
  %z = phi i1 [true, %entry], [%flag, %loop]
  %xx = zext i8 %x to i32
  %yy = zext i8 %y to i32
  %zz = zext i1 %z to i32
  %hi = shl i32 %xx, 8
  %tag = shl i32 %zz, 16
  %lo = or i32 %hi, %yy
  %result = or i32 %lo, %tag
  ret i32 %result
}

define i32 @main() {
entry:
  %r0 = call i32 @rotate(i64 0)
  %r1 = call i32 @rotate(i64 1)
  %r7 = call i32 @rotate(i64 7)
  %r8 = call i32 @rotate(i64 8)
  %a = icmp eq i32 %r0, 123
  %b = icmp eq i32 %r1, 123
  %c = icmp eq i32 %r7, 123
  %d = icmp eq i32 %r8, 231
  %ab = and i1 %a, %b
  %cd = and i1 %c, %d
  %old = and i1 %ab, %cd
  %f0 = call i32 @rotate_five(i16 0)
  %f1 = call i32 @rotate_five(i16 1)
  %f2 = call i32 @rotate_five(i16 2)
  %f7 = call i32 @rotate_five(i16 7)
  %f8 = call i32 @rotate_five(i16 8)
  %c0 = icmp eq i32 %f0, 12345
  %c1 = icmp eq i32 %f1, 12345
  %c2 = icmp eq i32 %f2, 23451
  %c7 = icmp eq i32 %f7, 23451
  %c8 = icmp eq i32 %f8, 34512
  %c01 = and i1 %c0, %c1
  %c27 = and i1 %c2, %c7
  %c0127 = and i1 %c01, %c27
  %five = and i1 %c0127, %c8
  %all = and i1 %old, %five
  %n0 = call i32 @narrow_cycle(i16 0)
  %n1 = call i32 @narrow_cycle(i16 1)
  %n2 = call i32 @narrow_cycle(i16 2)
  %n7 = call i32 @narrow_cycle(i16 7)
  %n8 = call i32 @narrow_cycle(i16 8)
  %ok0 = icmp eq i32 %n0, 130944
  %ok1 = icmp eq i32 %n1, 130944
  %ok2 = icmp eq i32 %n2, 33023
  %ok7 = icmp eq i32 %n7, 130944
  %ok8 = icmp eq i32 %n8, 33023
  %ok01 = and i1 %ok0, %ok1
  %ok27 = and i1 %ok2, %ok7
  %ok0127 = and i1 %ok01, %ok27
  %narrow = and i1 %ok0127, %ok8
  %complete = and i1 %all, %narrow
  %failed = xor i1 %complete, true
  %status = zext i1 %failed to i32
  ret i32 %status
}
