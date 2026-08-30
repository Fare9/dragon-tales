; ModuleID = 'sum_to_n_static'
source_filename = "sum_to_n_static"

define void @sub_2050(ptr %mem, ptr %regs) {
bb_8272:
  %0 = getelementptr i8, ptr %regs, i64 24
  %1 = load i64, ptr %0, align 4
  %2 = getelementptr i8, ptr %regs, i64 32
  %3 = load i64, ptr %2, align 4
  %4 = getelementptr i8, ptr %regs, i64 40
  %5 = load i64, ptr %4, align 4
  %6 = getelementptr i8, ptr %regs, i64 56
  %7 = load i32, ptr %6, align 4
  %8 = add i64 %3, -8
  %9 = getelementptr i8, ptr %mem, i64 %8
  store i64 %1, ptr %9, align 4
  br label %bb_8288

bb_8288:                                          ; preds = %bb_8587, %bb_8272
  %EDI.0 = phi i32 [ 0, %bb_8272 ], [ %24, %bb_8587 ]
  %RCX.0 = phi i64 [ 0, %bb_8272 ], [ %RCX.2, %bb_8587 ]
  %ECX.0 = phi i32 [ 0, %bb_8272 ], [ %ECX.1, %bb_8587 ]
  br i1 true, label %bb_8316, label %bb_8315

bb_8315:                                          ; preds = %bb_8288
  br i1 false, label %bb_8315.bb_8457_crit_edge, label %bb_8315.bb_8426_crit_edge

bb_8315.bb_8426_crit_edge:                        ; preds = %bb_8315
  br label %bb_8426

bb_8315.bb_8457_crit_edge:                        ; preds = %bb_8315
  br label %bb_8457

bb_8316:                                          ; preds = %bb_8288
  %10 = getelementptr i8, ptr %9, i64 -16
  store i64 0, ptr %10, align 4
  br i1 false, label %bb_8316.bb_8457_crit_edge, label %bb_8426

bb_8316.bb_8457_crit_edge:                        ; preds = %bb_8316
  br label %bb_8457

bb_8426:                                          ; preds = %bb_8315.bb_8426_crit_edge, %bb_8316
  br i1 true, label %bb_8535, label %bb_8426.bb_8457_crit_edge

bb_8426.bb_8457_crit_edge:                        ; preds = %bb_8426
  br label %bb_8457

bb_8457:                                          ; preds = %bb_8426.bb_8457_crit_edge, %bb_8316.bb_8457_crit_edge, %bb_8315.bb_8457_crit_edge
  br label %bb_8535

bb_8535:                                          ; preds = %bb_8457, %bb_8426
  %11 = add i32 %EDI.0, -2
  %12 = sub i32 1, %EDI.0
  %13 = and i32 %EDI.0, %12
  %.unshifted = xor i32 %11, %13
  %14 = icmp sgt i32 %.unshifted, -1
  br i1 %14, label %bb_8600, label %bb_8539

bb_8539:                                          ; preds = %bb_8535
  %15 = icmp slt i32 %7, 1
  br i1 %15, label %bb_8587, label %bb_8549

bb_8549:                                          ; preds = %bb_8539
  br i1 true, label %bb_8576, label %bb_8555

bb_8555:                                          ; preds = %bb_8549
  br i1 poison, label %bb_8555.bb_8587_crit_edge, label %bb_8567

bb_8555.bb_8587_crit_edge:                        ; preds = %bb_8555
  br label %bb_8587

bb_8567:                                          ; preds = %bb_8555
  br label %bb_8576

bb_8576:                                          ; preds = %bb_8576, %bb_8567, %bb_8549
  %RCX.1 = phi i64 [ %RCX.0, %bb_8549 ], [ %20, %bb_8576 ], [ poison, %bb_8567 ]
  %RAX.2 = phi i64 [ 1, %bb_8549 ], [ %22, %bb_8576 ], [ poison, %bb_8567 ]
  %EAX.1 = phi i32 [ 1, %bb_8549 ], [ %21, %bb_8576 ], [ poison, %bb_8567 ]
  %16 = shl i64 %RAX.2, 1
  %17 = add i64 %RCX.1, %16
  %18 = trunc i64 %17 to i32
  %19 = add i32 %18, 1
  %20 = zext i32 %19 to i64
  %21 = add i32 %EAX.1, 2
  %22 = zext i32 %21 to i64
  %23 = add i32 %EAX.1, 1
  %.not = icmp eq i32 %23, %7
  br i1 %.not, label %bb_8587, label %bb_8576

bb_8587:                                          ; preds = %bb_8555.bb_8587_crit_edge, %bb_8576, %bb_8539
  %RCX.2 = phi i64 [ %RCX.0, %bb_8539 ], [ %20, %bb_8576 ], [ poison, %bb_8555.bb_8587_crit_edge ]
  %ECX.1 = phi i32 [ %ECX.0, %bb_8539 ], [ %19, %bb_8576 ], [ poison, %bb_8555.bb_8587_crit_edge ]
  %24 = add i32 %EDI.0, 1
  br label %bb_8288

bb_8600:                                          ; preds = %bb_8535
  br i1 true, label %bb_8628, label %bb_8627

bb_8627:                                          ; preds = %bb_8600
  br i1 poison, label %bb_8627.bb_8758_crit_edge, label %bb_8627.bb_8732_crit_edge

bb_8627.bb_8732_crit_edge:                        ; preds = %bb_8627
  br label %bb_8732

bb_8627.bb_8758_crit_edge:                        ; preds = %bb_8627
  br label %bb_8758

bb_8628:                                          ; preds = %bb_8600
  %25 = getelementptr i8, ptr %mem, i64 %3
  %26 = getelementptr i8, ptr %25, i64 -16
  store i64 0, ptr %26, align 4
  br i1 false, label %bb_8628.bb_8758_crit_edge, label %bb_8732

bb_8628.bb_8758_crit_edge:                        ; preds = %bb_8628
  br label %bb_8758

bb_8732:                                          ; preds = %bb_8627.bb_8732_crit_edge, %bb_8628
  br i1 true, label %bb_8806, label %bb_8732.bb_8758_crit_edge

bb_8732.bb_8758_crit_edge:                        ; preds = %bb_8732
  br label %bb_8758

bb_8758:                                          ; preds = %bb_8732.bb_8758_crit_edge, %bb_8628.bb_8758_crit_edge, %bb_8627.bb_8758_crit_edge
  br label %bb_8806

bb_8806:                                          ; preds = %bb_8758, %bb_8732
  %27 = zext i32 %ECX.0 to i64
  store i64 %27, ptr %regs, align 4
  %28 = getelementptr i8, ptr %regs, i64 8
  store i64 %RCX.0, ptr %28, align 4
  %29 = getelementptr i8, ptr %regs, i64 16
  store i64 0, ptr %29, align 4
  store i64 %1, ptr %0, align 4
  store i64 %3, ptr %2, align 4
  store i64 %5, ptr %4, align 4
  %30 = getelementptr i8, ptr %regs, i64 64
  store i64 0, ptr %30, align 4
  %31 = getelementptr i8, ptr %regs, i64 136
  store i8 1, ptr %31, align 1
  %32 = getelementptr i8, ptr %regs, i64 139
  store i8 0, ptr %32, align 1
  %33 = getelementptr i8, ptr %regs, i64 140
  store i8 1, ptr %33, align 1
  %34 = getelementptr i8, ptr %regs, i64 141
  store i8 0, ptr %34, align 1
  %35 = getelementptr i8, ptr %regs, i64 143
  store i64 0, ptr %35, align 4
  %36 = getelementptr i8, ptr %regs, i64 151
  store i64 0, ptr %36, align 4
  %37 = getelementptr i8, ptr %regs, i64 159
  store i64 0, ptr %37, align 4
  ret void
}

@fmt = private unnamed_addr constant [24 x i8] c"n=%d -> got=%d want=%d\0A\00"

declare i32 @printf(ptr, ...)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

define i32 @main() {
entry:
  %mem = alloca [65536 x i8], align 16
  %regs = alloca [320 x i8], align 16
  call void @llvm.memset.p0.i64(ptr %regs, i8 0, i64 320, i1 false)
  call void @llvm.memset.p0.i64(ptr %mem, i8 0, i64 65536, i1 false)

  %rsp_ptr = getelementptr i8, ptr %regs, i64 32
  store i64 2048, ptr %rsp_ptr
  %rbp_ptr = getelementptr i8, ptr %regs, i64 40
  store i64 2048, ptr %rbp_ptr

  %n_ptr.0 = getelementptr i8, ptr %regs, i64 56
  store i32 10, ptr %n_ptr.0
  call void @sub_2050(ptr %mem, ptr %regs)
  %result.0 = load i32, ptr %regs
  call i32 (ptr, ...) @printf(ptr @fmt, i32 10, i32 %result.0, i32 55)

  %rsp_ptr.1 = getelementptr i8, ptr %regs, i64 32
  store i64 2048, ptr %rsp_ptr.1
  %n_ptr.1 = getelementptr i8, ptr %regs, i64 56
  store i32 6, ptr %n_ptr.1
  call void @sub_2050(ptr %mem, ptr %regs)
  %result.1 = load i32, ptr %regs
  call i32 (ptr, ...) @printf(ptr @fmt, i32 6, i32 %result.1, i32 21)

  %rsp_ptr.2 = getelementptr i8, ptr %regs, i64 32
  store i64 2048, ptr %rsp_ptr.2
  %n_ptr.2 = getelementptr i8, ptr %regs, i64 56
  store i32 15, ptr %n_ptr.2
  call void @sub_2050(ptr %mem, ptr %regs)
  %result.2 = load i32, ptr %regs
  call i32 (ptr, ...) @printf(ptr @fmt, i32 15, i32 %result.2, i32 120)

  %rsp_ptr.3 = getelementptr i8, ptr %regs, i64 32
  store i64 2048, ptr %rsp_ptr.3
  %n_ptr.3 = getelementptr i8, ptr %regs, i64 56
  store i32 1, ptr %n_ptr.3
  call void @sub_2050(ptr %mem, ptr %regs)
  %result.3 = load i32, ptr %regs
  call i32 (ptr, ...) @printf(ptr @fmt, i32 1, i32 %result.3, i32 1)

  ret i32 0
}