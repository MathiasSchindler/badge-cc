target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

define i32 @irreducible(i1 %choose) {
entry:
  %p = alloca i32, align 4
  %count = alloca i32, align 4
  store i32 1, ptr %p, align 4
  store i32 0, ptr %count, align 4
  br i1 %choose, label %a, label %b
a:
  %av = load i32, ptr %p, align 4
  %an = add i32 %av, 1
  store i32 %an, ptr %p, align 4
  br label %b
b:
  %bv = load i32, ptr %p, align 4
  %bn = add i32 %bv, 3
  store i32 %bn, ptr %p, align 4
  %cv = load i32, ptr %count, align 4
  %cn = add i32 %cv, 1
  store i32 %cn, ptr %count, align 4
  %again = icmp ult i32 %cn, 4
  br i1 %again, label %a, label %exit
exit:
  %result = load i32, ptr %p, align 4
  ret i32 %result
}

define i32 @parallel(i32 %x) {
entry:
  %p = alloca i32, align 4
  switch i32 %x, label %right [
    i32 0, label %left
    i32 1, label %left
  ]
left:
  store i32 7, ptr %p, align 4
  %c = icmp eq i32 %x, 0
  br i1 %c, label %join, label %join
right:
  store i32 11, ptr %p, align 4
  br label %join
join:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

define i32 @main() {
entry:
  %a = call i32 @irreducible(i1 true)
  %ok_a = icmp eq i32 %a, 17
  %b = call i32 @irreducible(i1 false)
  %ok_b = icmp eq i32 %b, 16
  %c = call i32 @parallel(i32 0)
  %ok_c = icmp eq i32 %c, 7
  %d = call i32 @parallel(i32 1)
  %ok_d = icmp eq i32 %d, 7
  %e = call i32 @parallel(i32 2)
  %ok_e = icmp eq i32 %e, 11
  %ab = and i1 %ok_a, %ok_b
  %cd = and i1 %ok_c, %ok_d
  %abcd = and i1 %ab, %cd
  %all = and i1 %abcd, %ok_e
  %result = select i1 %all, i32 0, i32 1
  ret i32 %result
}
