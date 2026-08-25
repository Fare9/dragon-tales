// Dumps every opcode name LLVM's X86 MC layer knows about, one per line.
//
// Ground truth for scripts/audit_x86_lifter_coverage.py, which cross-
// references this list against X86IGNILLifter.cpp's opcode dispatch table
// to find lifter gaps - mnemonic variants (rr/ri/ri8/rm/mr/mi/mi8, ...)
// LLVM can decode but the lifter doesn't recognize yet. This is what would
// have caught ToDo/LifterGaps.md #1 (mem-destination MOV/ADD/SUB/CMP)
// automatically instead of by manual discovery.

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>

#include <iostream>
#include <memory>
#include <string>

int main() {
  LLVMInitializeX86TargetInfo();
  LLVMInitializeX86Target();
  LLVMInitializeX86TargetMC();

  std::string error;
  const llvm::Target *target =
          llvm::TargetRegistry::lookupTarget("x86_64-unknown-unknown", error);
  if (!target) {
    std::cerr << "opcode_audit: " << error << "\n";
    return 1;
  }

  std::unique_ptr<llvm::MCInstrInfo> instrInfo(target->createMCInstrInfo());
  for (unsigned i = 0; i < instrInfo->getNumOpcodes(); ++i)
    std::cout << instrInfo->getName(i).str() << "\n";

  return 0;
}
