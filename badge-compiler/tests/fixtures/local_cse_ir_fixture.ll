target triple = "thumbv8m.main-unknown-none-eabi"
target datalayout = "e-m:e-p:32:32-Fi8-i64:64-v128:64:128-a:0:32-n32-S64"

@effects = global i32 0
@array = global [8 x i32] [i32 17, i32 18, i32 19, i32 20, i32 21, i32 22, i32 23, i32 24]

define void @touch() {
entry:
  %n = load volatile i32, ptr @effects
  %next = add i32 %n, 1
  store volatile i32 %next, ptr @effects
  ret void
}

define i32 @expression(i32 %x) {
entry:
  %a = add nsw nuw i32 %x, 1
  %b = mul nsw nuw i32 %a, 3
  call void @touch()
  %c = add nsw nuw i32 %x, 1
  %d = mul nsw nuw i32 %c, 3
  %r = xor i32 %b, %d
  ret i32 %r
}

define i64 @casts(i8 %x) {
entry:
  %a = sext i8 %x to i64
  %b = sext i8 %x to i64
  %r = add i64 %a, %b
  ret i64 %r
}

define i32 @addresses(i32 %i) {
entry:
  %a = getelementptr inbounds [8 x i32], ptr @array, i32 0, i32 %i
  %b = getelementptr inbounds [8 x i32], ptr @array, i32 0, i32 %i
  %same = icmp eq ptr %a, %b
  %first = load volatile i32, ptr %a
  %second = load volatile i32, ptr %b
  %sum = add i32 %first, %second
  %r = select i1 %same, i32 %sum, i32 0
  ret i32 %r
}

define i32 @division(i32 %x) {
entry:
  %a = udiv i32 %x, 2
  %b = udiv i32 %x, 2
  %r = add i32 %a, %b
  ret i32 %r
}

define i32 @main() {
entry:
  br label %loop
loop:
  %n = phi i32 [ 0, %entry ], [ %next, %good ]
  %a = call i32 @expression(i32 %n)
  %aok = icmp eq i32 %a, 0
  %byte = trunc i32 %n to i8
  %b = call i64 @casts(i8 %byte)
  %wide = sext i8 %byte to i64
  %twice = mul i64 %wide, 2
  %bok = icmp eq i64 %b, %twice
  %index = and i32 %n, 7
  %c = call i32 @addresses(i32 %index)
  %value = add i32 %index, 17
  %expected = mul i32 %value, 2
  %cok = icmp eq i32 %c, %expected
  %q = call i32 @division(i32 %n)
  %even = and i32 %n, 4294967294
  %qok = icmp eq i32 %q, %even
  %ab = and i1 %aok, %bok
  %cq = and i1 %cok, %qok
  %ok = and i1 %ab, %cq
  br i1 %ok, label %good, label %bad
good:
  %next = add i32 %n, 1
  %again = icmp ult i32 %next, 256
  br i1 %again, label %loop, label %out
bad:
  ret i32 1
out:
  %calls = load volatile i32, ptr @effects
  %callsok = icmp eq i32 %calls, 256
  %code = select i1 %callsok, i32 0, i32 1
  ret i32 %code
}
