target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"
@boolean_guard = global i64 0, align 8

define i1 @booleans(i1 %x) {
entry:
  %a = zext i1 %x to i8
  %at = icmp ne i8 %a, 0
  %b = zext i1 %at to i16
  %bt = icmp eq i16 1, %b
  %c = sext i1 %bt to i32
  %ct = icmp ne i32 0, %c
  %d = sext i1 %ct to i64
  store volatile i64 %d, ptr @boolean_guard, align 8
  %dt = icmp eq i64 %d, -1
  ret i1 %dt
}

define i64 @wide(i64 %x) {
entry:
  %a = add nsw nuw i64 %x, 0
  %b = sub nsw nuw i64 %a, 0
  %c = mul nsw nuw i64 %b, 1
  %d = and i64 %c, -1
  %e = or disjoint i64 0, %d
  %f = xor i64 %e, 0
  %g = shl nsw nuw i64 %f, 0
  %h = lshr exact i64 %g, 0
  %i = ashr exact i64 %h, 0
  ret i64 %i
}

define i64 @signed(i8 %x) {
entry:
  %a = sext i8 %x to i16
  %b = sext i16 %a to i64
  ret i64 %b
}

define i64 @unsigned(i8 %x) {
entry:
  %a = zext i8 %x to i16
  %b = sext i16 %a to i64
  ret i64 %b
}

define i16 @not_combined(i8 %x) {
entry:
  %a = sext i8 %x to i16
  %b = zext i16 %a to i32
  %c = trunc i32 %b to i16
  ret i16 %c
}

define i8 @roundtrip(i8 %x) {
entry:
  %a = zext i8 %x to i64
  %b = add i64 %a, 0
  %c = trunc i64 %b to i8
  ret i8 %c
}

define i32 @main() {
entry:
  %min = call i64 @wide(i64 9223372036854775808)
  %minok = icmp eq i64 %min, 9223372036854775808
  %max = call i64 @wide(i64 18446744073709551615)
  %maxok = icmp eq i64 %max, -1
  br label %loop
loop:
  %n = phi i32 [ 0, %entry ], [ %next, %good ]
  %x = trunc i32 %n to i8
  %s = call i64 @signed(i8 %x)
  %expected_s = sext i8 %x to i64
  %sok = icmp eq i64 %s, %expected_s
  %u = call i64 @unsigned(i8 %x)
  %expected_u = zext i8 %x to i64
  %uok = icmp eq i64 %u, %expected_u
  %r = call i8 @roundtrip(i8 %x)
  %rok = icmp eq i8 %r, %x
  %p = call i16 @not_combined(i8 %x)
  %expected_p = sext i8 %x to i16
  %pok = icmp eq i16 %p, %expected_p
  %flag = icmp ne i8 %x, 0
  %boolean = call i1 @booleans(i1 %flag)
  %boolok = icmp eq i1 %boolean, %flag
  %guard = load volatile i64, ptr @boolean_guard, align 8
  %expected_guard = sext i1 %flag to i64
  %guardok = icmp eq i64 %guard, %expected_guard
  %a = and i1 %sok, %uok
  %b = and i1 %rok, %pok
  %c = and i1 %a, %b
  %d = and i1 %boolok, %guardok
  %all = and i1 %c, %d
  br i1 %all, label %good, label %bad
good:
  %next = add i32 %n, 1
  %again = icmp ult i32 %next, 256
  br i1 %again, label %loop, label %out
bad:
  ret i32 1
out:
  %ok = and i1 %minok, %maxok
  %code = select i1 %ok, i32 0, i32 1
  ret i32 %code
}
