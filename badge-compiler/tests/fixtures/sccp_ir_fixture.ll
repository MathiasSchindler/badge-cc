target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

@effects = global i32 0

define void @effect() {
entry:
  %n = load volatile i32, ptr @effects
  %next = add i32 %n, 1
  store volatile i32 %next, ptr @effects
  ret void
}

define i32 @choose() {
entry:
  %c = icmp eq i64 -1, 18446744073709551615
  br i1 %c, label %left, label %dead
dead:
  call void @effect()
  br label %join
left:
  br label %join
join:
  %n = phi i32 [ 2, %left ], [ 0, %dead ]
  switch i32 %n, label %bad [
    i32 1, label %bad
    i32 2, label %good
    i32 3, label %good
  ]
bad:
  call void @effect()
  ret i32 9
good:
  %v = phi i32 [ 7, %join ], [ 7, %join ]
  ret i32 %v
}

define i32 @count() {
entry:
  br label %loop
loop:
  %n = phi i32 [ 0, %entry ], [ %next, %loop ]
  %next = add i32 %n, 1
  %c = icmp ult i32 %next, 4
  br i1 %c, label %loop, label %out
out:
  ret i32 %next
}

define i32 @main() {
entry:
  %a = call i32 @choose()
  %aok = icmp eq i32 %a, 7
  %b = call i32 @count()
  %bok = icmp eq i32 %b, 4
  call void @effect()
  %v = load volatile i32, ptr @effects
  %vok = icmp eq i32 %v, 1
  %ab = and i1 %aok, %bok
  %ok = and i1 %ab, %vok
  %code = select i1 %ok, i32 0, i32 1
  ret i32 %code
}
