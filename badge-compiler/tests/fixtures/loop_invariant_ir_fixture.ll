target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
@loop_guard = global i32 0, align 4

define i32 @loop_divide(i32 %x, i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [0, %entry], [%result, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %exit
body:
  %q = udiv i32 %x, 7
  %result = add i32 %acc, %q
  %next = add i32 %i, 1
  br label %header
exit:
  ret i32 %acc
}

define i32 @loop_remainder(i32 %x, i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [0, %entry], [%result, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %exit
body:
  %q = srem i32 %x, -7
  %result = add i32 %acc, %q
  %next = add i32 %i, 1
  br label %header
exit:
  ret i32 %acc
}

define i32 @loop_sum(i32 %x, i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [0, %entry], [%result, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %exit
body:
  %m = mul i32 %x, 7
  %a = add i32 %m, 11
  %q = udiv i32 %x, 7
  %signed = sdiv i32 %x, 7
  %b = add i32 %a, %q
  %v = add i32 %b, %signed
  %result = add i32 %acc, %v
  %g = load volatile i32, ptr @loop_guard, align 4
  %gn = add i32 %g, 1
  store volatile i32 %gn, ptr @loop_guard, align 4
  %next = add i32 %i, 1
  br label %header
exit:
  ret i32 %acc
}

define i32 @loop_nested(i32 %x, i32 %n, i32 %m) {
entry:
  br label %outer
outer:
  %i = phi i32 [0, %entry], [%next, %after]
  %acc = phi i32 [0, %entry], [%inneracc, %after]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %pre, label %exit
pre:
  br label %inner
inner:
  %j = phi i32 [0, %pre], [%jn, %body]
  %inneracc = phi i32 [%acc, %pre], [%result, %body]
  %more = icmp ult i32 %j, %m
  br i1 %more, label %body, label %after
body:
  %a = mul i32 %x, 7
  %v = add i32 %a, %i
  %result = add i32 %inneracc, %v
  %jn = add i32 %j, 1
  br label %inner
after:
  %next = add i32 %i, 1
  br label %outer
exit:
  ret i32 %acc
}

define i32 @loop_variable(i32 %x, i32 %n, i32 %d) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [0, %entry], [%result, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %exit
body:
  %q = udiv i32 %x, %d
  %result = add i32 %acc, %q
  %next = add i32 %i, 1
  br label %header
exit:
  ret i32 %acc
}

define i32 @loop_overflow(i32 %x, i32 %n) {
entry:
  br label %header
header:
  %i = phi i32 [0, %entry], [%next, %body]
  %acc = phi i32 [0, %entry], [%result, %body]
  %go = icmp ult i32 %i, %n
  br i1 %go, label %body, label %exit
body:
  %q = sdiv i32 %x, -1
  %result = add i32 %acc, %q
  %next = add i32 %i, 1
  br label %header
exit:
  ret i32 %acc
}

define i32 @loop_irreducible(i32 %x, i1 %choose) {
entry:
  br i1 %choose, label %a, label %b
a:
  %ca = phi i32 [0, %entry], [%nb, %b]
  %va = mul i32 %x, 7
  %na = add i32 %ca, 1
  br label %b
b:
  %cb = phi i32 [0, %entry], [%na, %a]
  %vb = add i32 %x, 11
  %nb = add i32 %cb, 1
  %more = icmp ult i32 %nb, 5
  br i1 %more, label %a, label %exit
exit:
  ret i32 %vb
}
