; ModuleID = 'vm_devirt.ll'
source_filename = "vm_devirt.ll"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef i32 @check_serial_devirt(ptr readnone captures(none) %serial) local_unnamed_addr #0 {
entry:
  ret i32 0
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) }
