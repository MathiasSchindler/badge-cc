target triple = "thumbv8m.main-unknown-none-eabi"

declare void @llvm.memcpy.inline.p0.p0.i32(ptr, ptr, i32, i1)
declare void @llvm.memset.inline.p0.i32(ptr, i8, i32, i1)

define void @memory_copy(ptr %out, i32 %which) {
entry:
  %a = alloca [96 x i8], align 4
  %b = alloca [96 x i8], align 4
  %dst = getelementptr [96 x i8], ptr %a, i32 0, i32 4
  %src = getelementptr [96 x i8], ptr %b, i32 0, i32 4
  call void @llvm.memset.inline.p0.i32(ptr %a, i8 -91, i32 96, i1 false)
  br label %init
init:
  %i = phi i32 [0, %entry], [%next, %init]
  %p = getelementptr [96 x i8], ptr %b, i32 0, i32 %i
  %m = mul i32 %i, 37
  %v = add i32 %m, 11
  %byte = trunc i32 %v to i8
  store i8 %byte, ptr %p, align 1
  %next = add i32 %i, 1
  %more = icmp ult i32 %next, 96
  br i1 %more, label %init, label %choose
choose:
  switch i32 %which, label %copy65 [
    i32 0, label %copy0
    i32 1, label %copy1
    i32 2, label %copy2
    i32 3, label %copy3
    i32 4, label %copy4
    i32 5, label %copy5
    i32 6, label %copy7
    i32 7, label %copy8
    i32 8, label %copy9
    i32 9, label %copy15
    i32 10, label %copy16
    i32 11, label %copy17
    i32 12, label %copy63
    i32 13, label %copy64
  ]
copy0:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 0, i1 false)
  br label %done
copy1:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 1, i1 false)
  br label %done
copy2:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 2, i1 false)
  br label %done
copy3:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 3, i1 false)
  br label %done
copy4:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 4, i1 false)
  br label %done
copy5:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 5, i1 false)
  br label %done
copy7:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 7, i1 false)
  br label %done
copy8:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 8, i1 false)
  br label %done
copy9:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 9, i1 false)
  br label %done
copy15:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 15, i1 false)
  br label %done
copy16:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 16, i1 false)
  br label %done
copy17:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 17, i1 false)
  br label %done
copy63:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 63, i1 false)
  br label %done
copy64:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 64, i1 false)
  br label %done
copy65:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 65, i1 false)
  br label %done
done:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %out, ptr %a, i32 96, i1 false)
  ret void
}

define void @memory_fill(ptr %out, i8 %fill, i32 %which) {
entry:
  %a = alloca [96 x i8], align 4
  %dst = getelementptr [96 x i8], ptr %a, i32 0, i32 4
  call void @llvm.memset.inline.p0.i32(ptr %a, i8 -91, i32 96, i1 false)
  switch i32 %which, label %fill65 [
    i32 0, label %fill0
    i32 1, label %fill1
    i32 2, label %fill2
    i32 3, label %fill3
    i32 4, label %fill4
    i32 5, label %fill5
    i32 6, label %fill7
    i32 7, label %fill8
    i32 8, label %fill9
    i32 9, label %fill15
    i32 10, label %fill16
    i32 11, label %fill17
    i32 12, label %fill63
    i32 13, label %fill64
  ]
fill0:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 0, i1 false)
  br label %done
fill1:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 1, i1 false)
  br label %done
fill2:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 2, i1 false)
  br label %done
fill3:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 3, i1 false)
  br label %done
fill4:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 4, i1 false)
  br label %done
fill5:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 5, i1 false)
  br label %done
fill7:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 7, i1 false)
  br label %done
fill8:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 8, i1 false)
  br label %done
fill9:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 9, i1 false)
  br label %done
fill15:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 15, i1 false)
  br label %done
fill16:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 16, i1 false)
  br label %done
fill17:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 17, i1 false)
  br label %done
fill63:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 63, i1 false)
  br label %done
fill64:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 64, i1 false)
  br label %done
fill65:
  call void @llvm.memset.inline.p0.i32(ptr %dst, i8 %fill, i32 65, i1 false)
  br label %done
done:
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %out, ptr %a, i32 96, i1 false)
  ret void
}

define void @memory_fallback(ptr %out) {
entry:
  %a = alloca [96 x i8], align 4
  %b = alloca [96 x i8], align 4
  %aligned_dst = getelementptr [96 x i8], ptr %a, i32 0, i32 4
  %aligned_src = getelementptr [96 x i8], ptr %b, i32 0, i32 4
  %dst = getelementptr [96 x i8], ptr %a, i32 0, i32 5
  %src = getelementptr [96 x i8], ptr %b, i32 0, i32 5
  call void @llvm.memset.inline.p0.i32(ptr %a, i8 -91, i32 96, i1 false)
  call void @llvm.memset.inline.p0.i32(ptr %b, i8 51, i32 96, i1 false)
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %aligned_dst, ptr %aligned_src, i32 7, i1 true)
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %dst, ptr %src, i32 17, i1 false)
  call void @llvm.memcpy.inline.p0.p0.i32(ptr %out, ptr %a, i32 96, i1 false)
  ret void
}
