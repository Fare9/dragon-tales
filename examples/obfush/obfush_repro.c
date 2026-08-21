/* Integer-only reproduction of obfus.h's "Virtualization" (VIRT) feature -
 * https://github.com/DosX-dev/obfus.h, cloned locally to /opt/work/obfus.h.
 *
 * The real obfus.h uses `long double` (x87 extended precision) throughout,
 * which forces fld/fstp/x87 instructions - dragon-tales's X86IGNILLifter has
 * no x87 or SSE support at all yet, so the real compiled VM_ADD/etc call
 * sites are not liftable today (see ToDo/Roadmap.md's "Not started" list).
 * This file is NOT the real obfus.h - it's a deliberate, honest adaptation
 * using `int` instead of `long double`, preserving the exact same
 * structural pattern:
 *   - a compile-time-constant "encrypted" opcode per call site
 *     (real formula: (op - 1) * ~SALT_CMD, from
 *     obfus.h/experiments/obfus-gcc-by-CyberForgeEx.h's _VM_ENCRYPT_INT)
 *   - each operand masked at the call site via negate-and-offset
 *     (real formula: (num) * -1 + SALT)
 *   - one shared function that undoes both and dispatches via switch
 *
 * This is the SAME fixture used in tests/obfush/TestObfush.cpp (which
 * embeds `compute`'s compiled bytes directly and runs dragon-tales's
 * BackwardSlicer + Z3 against them to recover the masking formulas and
 * encrypted opcode). This copy adds a real `main()` so it's a complete,
 * standalone, runnable program - meant for independent manual analysis
 * (e.g. in Binary Ninja), not for the automated test suite.
 *
 * Compiled with: gcc -O2 -masm=intel obfush_repro.c -o obfush_repro -w
 * (GCC 15.2.0, Ubuntu 15.2.0-16ubuntu1, x86-64 Linux) - the same flags the
 * real obfus.h GCC-compatible fork itself recommends.
 */

#include <stdio.h>

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

int main() {
    int result = compute(15, 7);
    printf("compute(15, 7) = %d (expect 22)\n", result);
    return 0;
}
