target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

define i32 @entry_store(i32 %x, i1 %choose) {
entry:
  %p = alloca i32, align 4
  store i32 %x, ptr %p, align 4
  br i1 %choose, label %left, label %right
left:
  %a = load i32, ptr %p, align 4
  %b = add i32 %a, 1
  br label %join
right:
  %c = load i32, ptr %p, align 4
  %d = sub i32 %c, 1
  br label %join
join:
  %r = phi i32 [%b, %left], [%d, %right]
  %v = load i32, ptr %p, align 4
  %sum = add i32 %r, %v
  ret i32 %sum
}

define i32 @block_local() {
entry:
  %p = alloca i32, align 4
  br label %loop
loop:
  %k = phi i32 [0, %entry], [%next, %loop]
  %total = phi i32 [0, %entry], [%sum, %loop]
  store i32 %k, ptr %p, align 4
  %v = load i32, ptr %p, align 4
  %sum = add i32 %total, %v
  %next = add i32 %k, 1
  %again = icmp ult i32 %next, 10
  br i1 %again, label %loop, label %exit
exit:
  ret i32 %sum
}

define i32 @entry_wide(i64 %x, i1 %choose) {
entry:
  %p = alloca i64, align 8
  store i64 %x, ptr %p, align 8
  br i1 %choose, label %left, label %right
left:
  %a = load i64, ptr %p, align 8
  %b = add i64 %a, 4294967297
  br label %join
right:
  %c = load i64, ptr %p, align 8
  %d = sub i64 %c, 4294967297
  br label %join
join:
  %r = phi i64 [%b, %left], [%d, %right]
  %v = load i64, ptr %p, align 8
  %different = xor i64 %r, %v
  %lo = trunc i64 %different to i32
  %shifted = lshr i64 %different, 32
  %hi = trunc i64 %shifted to i32
  %folded = xor i32 %lo, %hi
  ret i32 %folded
}

define i32 @main() {
entry:
  %a = call i32 @entry_store(i32 77, i1 true)
  %ok_a = icmp eq i32 %a, 155
  %b = call i32 @entry_store(i32 -3, i1 false)
  %ok_b = icmp eq i32 %b, -7
  %c = call i32 @block_local()
  %ok_c = icmp eq i32 %c, 45
  %d = call i32 @entry_wide(i64 0, i1 true)
  %ok_d = icmp eq i32 %d, 0
  %e = call i32 @entry_wide(i64 0, i1 false)
  %ok_e = icmp eq i32 %e, 1
  %ab = and i1 %ok_a, %ok_b
  %cd = and i1 %ok_c, %ok_d
  %abcd = and i1 %ab, %cd
  %all = and i1 %abcd, %ok_e
  %result = select i1 %all, i32 0, i32 1
  ret i32 %result
}
