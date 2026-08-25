/* Integer-only reproduction of obfus.h's "Virtualization" (VIRT) feature -
 * https://github.com/DosX-dev/obfus.h, cloned locally to /opt/work/obfus.h.
 *
 * The real obfus.h uses `long double` (x87 extended precision) throughout,
 * which forces fld/fstp/x87 instructions - X86IGNILLifter.cpp has no x87 or
 * SSE support at all (grepped for it, nothing), so the real compiled
 * VM_ADD/etc call sites are not liftable today. This file is NOT the real
 * obfus.h - it's a deliberate, honest adaptation using `int` instead of
 * `long double`, preserving the exact same structural pattern:
 *   - a compile-time-constant "encrypted" opcode per call site
 *     (real formula: (op - 1) * ~SALT_CMD, from
 *     obfus.h/experiments/obfus-gcc-by-CyberForgeEx.h's _VM_ENCRYPT_INT)
 *   - each operand masked at the call site via negate-and-offset
 *     (real formula: (num) * -1 + SALT)
 *   - one shared function that undoes both and dispatches via switch
 *
 * Compiled with: gcc -O2 -c obfush_repro.c -o obfush_repro.o -w
 * (GCC 15.2.0, Ubuntu 15.2.0-16ubuntu1, x86-64 Linux). `compute` gets
 * tail-call-optimized into `jmp VmDispatch` rather than `call` - see
 * tests/obfush/TestObfush.cpp for the exact resulting bytes and the
 * dragon-tales analysis run against them.
 */

#define SALT_CMD 0x1F4
#define SALT_NUM1 23
#define SALT_NUM2 31
#define ENCRYPT(op) (((op) - 1) * ~SALT_CMD)

typedef enum { OP_ADD = 4530, OP_SUB = 9530, OP_MUL = 14530, OP_DIV = 19530 } Op;

__attribute__((noinline))
int VmDispatch(int command, int num1, int num2) {
    command = command / ~SALT_CMD + 1;
    num1 = -num1 + SALT_NUM1;
    num2 = -num2 + SALT_NUM2;
    switch (command) {
        case OP_ADD: return num1 + num2;
        case OP_SUB: return num1 - num2;
        case OP_MUL: return num1 * num2;
        case OP_DIV: return num2 != 0 ? num1 / num2 : 0;
        default: return 0;
    }
}

#define VM_ADD(a, b) VmDispatch(ENCRYPT(OP_ADD), -(a) + SALT_NUM1, -(b) + SALT_NUM2)
#define VM_SUB(a, b) VmDispatch(ENCRYPT(OP_SUB), -(a) + SALT_NUM1, -(b) + SALT_NUM2)

__attribute__((noinline))
int compute(int a, int b) {
    return VM_ADD(a, b);
}
