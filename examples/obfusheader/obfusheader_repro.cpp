// Four small functions compiled with the REAL obfusheader.h
// (https://github.com/ac3ss0r/obfusheader.h, cloned locally to
// /opt/work/obfusheader.h) - unlike obfus.h, this one genuinely supports
// Linux natively (windows.h is properly gated behind #ifdef _WIN32, no
// mingw needed) and doesn't touch x87 long double anywhere.
//
// CFLOW_BRANCHING is enabled: it #defines if/for/while/switch/else
// themselves, wrapping every branch in the source in opaque predicates
// (int_proxy() - a real function using SSE2 `double` arithmetic that gets
// fully inlined, not x87). A single trivial if/else compiles to ~500
// instructions - real, substantial control-flow obfuscation, not the tiny
// arithmetic-masking exercise the obfus.h work looked at.
// CONST_ENCRYPTION is also enabled (OBF(x)): compile-time constant
// encryption via templates, so magic numbers never appear as plain
// immediates in the binary.
//
// Compiled with: g++ -O2 -std=c++17 -I/opt/work/obfusheader.h/include
// obfusheader_repro.cpp -o obfusheader_repro -w
// (same command as examples/obfush/, just g++ instead of gcc since
// obfusheader.h needs C++14+ for CONST_ENCRYPTION's templates)

#define CONST_ENCRYPTION 1
#define CFLOW_CONST_DECRYPTION 0
#define CFLOW_BRANCHING 1
#define INDIRECT_BRANCHING 0
#define FAKE_SIGNATURES 0
#define INLINE_STD 0
#include "obfusheader.h"

#include <cstdio>

// Exercises: comparison + branching (if/else).
int max_op(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

// Exercises: NEG + branching.
int abs_op(int a) {
    if (a < 0) {
        return -a;
    } else {
        return a;
    }
}

// Exercises: a real loop (INC via the loop counter, the fix from earlier
// today), plus a compile-time-encrypted constant as the bound.
int sum_to_n(int n) {
    int total = 0;
    for (int i = 1; i <= n; i++) {
        total += i;
    }
    return total;
}

// Exercises: CONST_ENCRYPTION on a magic-number comparison.
bool is_magic(int x) {
    if (x == OBF(0x1234)) {
        return true;
    } else {
        return false;
    }
}

int main() {
    printf("max_op(15,7)=%d abs_op(-9)=%d sum_to_n(10)=%d is_magic(0x1234)=%d "
           "is_magic(5)=%d\n",
           max_op(15, 7), abs_op(-9), sum_to_n(10), (int) is_magic(0x1234),
           (int) is_magic(5));
    return 0;
}
