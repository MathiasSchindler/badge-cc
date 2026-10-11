target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

define i32 @scalar_immediates(i32 %x) {
entry:
  %a = add i32 %x, 4095
  %b = sub i32 %a, -4095
  %c = and i32 %b, 16711935
  %d = or i32 %c, 2147483648
  %e = xor i32 %d, 1431655765
  %f = shl i32 %e, 5
  %g = lshr i32 %f, 3
  %h = ashr i32 %x, 31
  %j = xor i32 %g, %h
  %k = xor i32 %j, 305419896
  ret i32 %k
}
define i32 @scalar_mla(i32 %x, i32 %y, i32 %z) {
entry:
  %m = mul i32 %x, %y
  %a = add i32 %z, %m
  ret i32 %a
}
define i32 @scalar_multiuse(i32 %x, i32 %y, i32 %z) {
entry:
  %m = mul i32 %x, %y
  %a = add i32 %m, %z
  %r = xor i32 %a, %m
  ret i32 %r
}
define i32 @scalar_deferred(i32 %x, i32 %y, i32 %z) {
entry:
  %a = add i32 %x, 1
  %b = xor i32 %y, 1431655765
  %m = mul i32 %a, %b
  %r = add i32 %m, %z
  ret i32 %r
}
define i32 @scalar_deferred_division(i32 %x, i32 %d) {
entry:
  %n = add i32 %x, 3
  %q = udiv i32 %n, %d
  %r = urem i32 %n, %d
  %a = add i32 %q, %r
  ret i32 %a
}
define i32 @scalar_flagged(i32 %x) {
entry:
  %m = mul nsw i32 %x, 7
  %a = add nsw i32 %m, 19
  ret i32 %a
}
define i32 @scalar_unsigned(i32 %x, i32 %d) {
entry:
  %q = udiv i32 %x, %d
  %r = urem i32 %x, %d
  %a = add i32 %q, %r
  ret i32 %a
}
define i32 @scalar_signed_reverse(i32 %x, i32 %d) {
entry:
  %r = srem i32 %x, %d
  %q = sdiv i32 %x, %d
  %a = add i32 %q, %r
  ret i32 %a
}
define i32 @scalar_exact(i32 %x) {
entry:
  %q = udiv exact i32 %x, 8
  %r = urem i32 %x, 8
  %a = add i32 %q, %r
  ret i32 %a
}
define i32 @scalar_addresses(ptr %base, i32 %k) {
entry:
  %p = getelementptr i32, ptr %base, i32 %k
  %x = load i32, ptr %p, align 4
  %q = getelementptr {i32, [3 x i32]}, ptr %base, i32 0, i32 1, i32 2
  %y = load i32, ptr %q, align 4
  %a = add i32 %x, %y
  ret i32 %a
}
define i32 @scalar_negative_address(ptr %base) {
entry:
  %p = getelementptr i32, ptr %base, i32 -1
  %x = load i32, ptr %p, align 4
  ret i32 %x
}
define i32 @scalar_stride3(ptr %base, i32 %k) {
entry:
  %p = getelementptr [3 x i8], ptr %base, i32 %k
  %x = load i8, ptr %p, align 1
  %r = zext i8 %x to i32
  ret i32 %r
}

define i32 @scalar_register_mix(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = sub i32 %x, %y
  %c = mul i32 %a, %b
  %d = xor i32 %c, %x
  %e = and i32 %d, %y
  %f = or i32 %e, %b
  %g = xor i32 %f, %f
  %h = add i32 %f, %g
  ret i32 %h
}

define i32 @scalar_register_shifts(i32 %x, i32 %amount) {
entry:
  %a = shl i32 %x, %amount
  %b = lshr i32 %x, %amount
  %c = ashr i32 %x, %amount
  %d = xor i32 %a, %b
  %e = xor i32 %d, %c
  ret i32 %e
}

define i32 @scalar_register_zero(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, 0
  %b = sub i32 %y, 0
  %c = shl i32 %a, 0
  %d = lshr i32 %b, 0
  %e = ashr i32 %c, 0
  %f = xor i32 %d, %e
  %g = xor i32 %f, %x
  ret i32 %g
}
