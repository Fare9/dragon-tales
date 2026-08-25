import os
import re
import cffi

# -- cffi setup ----------------------------------------------------------------

_here = os.path.dirname(os.path.abspath(__file__))
_root = os.path.dirname(_here)

_HEADER = os.path.join(_root, 'c_shim', 'dragon_c.h')
_LIB = os.path.join(_root, 'build', 'c_shim', 'libdragon_c.so')

ffi = cffi.FFI()


def _prepare_cdef(path):
    with open(path) as f:
        src = f.read()
    # Remove #ifdef __cplusplus ... #endif blocks (includes extern "C" { })
    src = re.sub(r'#ifdef\s+__cplusplus.*?#endif', '', src, flags=re.DOTALL)
    # Remove remaining preprocessor lines (#pragma, #include, etc.)
    src = re.sub(r'^\s*#.*$', '', src, flags=re.MULTILINE)
    return src


ffi.cdef(_prepare_cdef(_HEADER))
lib = ffi.dlopen(_LIB)


# -- enums ---------------------------------------------------------------------

class Architecture:
    """
    Possible Architectures supported by dragon
    these are supported for assembly and disassembly.
    """
    UNKNOWN = 0
    X86_32 = 1
    X86_64 = 2
    ARM32 = 3
    ARM64 = 4
    RISCV32 = 5
    RISCV64 = 6


class Syntax:
    """
    Syntax used in configuration to show the disassembled code
    they can be INTEL or ATT.
    """
    INTEL = 0
    ATT = 1


class Strategy:
    """
    Strategies to use in the disassembler, for the moment only
    linear sweep and recursive traversal are available. Others
    like a hybrid approach will be implemented.
    """
    LINEAR_SWEEP = 0
    RECURSIVE_TRAVERSAL = 1


# -- Instruction ---------------------------------------------------------------

class Instruction:
    """Lightweight view into the graph listing — not owned, no destroy."""

    def __init__(self, ptr):
        self._ptr = ptr

    @property
    def address(self):
        return lib.dragon_instruction_address(self._ptr)

    @property
    def size(self):
        return lib.dragon_instruction_size(self._ptr)

    @property
    def mnemonic(self):
        return ffi.string(lib.dragon_instruction_mnemonic(self._ptr)).decode()

    @property
    def operands(self):
        return ffi.string(lib.dragon_instruction_operands(self._ptr)).decode()

    @property
    def disassembly(self):
        return ffi.string(lib.dragon_instruction_disassembly(self._ptr)).decode()

    @property
    def pattern(self):
        return ffi.string(lib.dragon_instruction_pattern(self._ptr)).decode()

    @property
    def bytes(self):
        length = ffi.new('size_t *')
        ptr = lib.dragon_instruction_bytes(self._ptr, length)
        return bytes(ffi.buffer(ptr, length[0]))

    @property
    def chromosome_mask(self):
        length = ffi.new('size_t *')
        ptr = lib.dragon_instruction_chromosome_mask(self._ptr, length)
        return bytes(ffi.buffer(ptr, length[0]))

    @property
    def is_block_start(self):    return bool(lib.dragon_instruction_is_block_start(self._ptr))

    @property
    def is_function_start(self): return bool(lib.dragon_instruction_is_function_start(self._ptr))

    @property
    def is_prologue(self):       return bool(lib.dragon_instruction_is_prologue(self._ptr))

    @property
    def is_return(self):         return bool(lib.dragon_instruction_is_return(self._ptr))

    @property
    def is_call(self):           return bool(lib.dragon_instruction_is_call(self._ptr))

    @property
    def is_jump(self):           return bool(lib.dragon_instruction_is_jump(self._ptr))

    @property
    def is_conditional(self):    return bool(lib.dragon_instruction_is_conditional(self._ptr))

    @property
    def is_trap(self):           return bool(lib.dragon_instruction_is_trap(self._ptr))

    @property
    def has_indirect_target(self): return bool(lib.dragon_instruction_has_indirect_target(self._ptr))

    @property
    def edges(self):             return lib.dragon_instruction_edges(self._ptr)

    @property
    def branches(self):
        result = []
        cb = ffi.callback('void(uint64_t, void *)', lambda addr, _: result.append(addr))
        lib.dragon_instruction_each_branch(self._ptr, cb, ffi.NULL)
        return result

    @property
    def callees(self):
        result = []
        cb = ffi.callback('void(uint64_t, void *)', lambda addr, _: result.append(addr))
        lib.dragon_instruction_each_callee(self._ptr, cb, ffi.NULL)
        return result

    def __repr__(self):
        return f'<Instruction 0x{self.address:x}: {self.disassembly}>'


# -- BasicBlock ----------------------------------------------------------------

class BasicBlock:
    """Heap-allocated view - owns its pointer, must be destroyed."""

    def __init__(self, ptr):
        self._ptr = ptr

    def __del__(self):
        if self._ptr:
            lib.dragon_block_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def is_valid(self):           return bool(lib.dragon_block_valid(self._ptr))

    @property
    def start_address(self):      return lib.dragon_block_start_address(self._ptr)

    @property
    def terminator_address(self): return lib.dragon_block_terminator_address(self._ptr)

    @property
    def size(self):               return lib.dragon_block_size(self._ptr)

    @property
    def byte_size(self):          return lib.dragon_block_byte_size(self._ptr)

    def instructions(self):
        result = []
        cb = ffi.callback('void(const dragon_instruction_t *, void *)',
                          lambda ptr, _: result.append(Instruction(ptr)))
        lib.dragon_block_each_instruction(self._ptr, cb, ffi.NULL)
        return result

    def successors(self):
        result = []
        cb = ffi.callback('void(uint64_t, void *)', lambda addr, _: result.append(addr))
        lib.dragon_block_each_successor(self._ptr, cb, ffi.NULL)
        return result

    def __repr__(self):
        return f'<BasicBlock 0x{self.start_address:x} valid={self.is_valid}>'


# -- Function ------------------------------------------------------------------

class Function:
    """Heap-allocated view - owns its pointer, must be destroyed."""

    def __init__(self, ptr):
        self._ptr = ptr

    def __del__(self):
        if self._ptr:
            lib.dragon_function_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def is_valid(self):              return bool(lib.dragon_function_valid(self._ptr))

    @property
    def start_address(self):         return lib.dragon_function_start_address(self._ptr)

    @property
    def num_blocks(self):            return lib.dragon_function_num_blocks(self._ptr)

    @property
    def num_instructions(self):      return lib.dragon_function_num_instructions(self._ptr)

    @property
    def edges(self):                 return lib.dragon_function_edges(self._ptr)

    @property
    def byte_size(self):             return lib.dragon_function_byte_size(self._ptr)

    @property
    def cyclomatic_complexity(self): return lib.dragon_function_cyclomatic_complexity(self._ptr)

    def block_addresses(self):
        """Return start addresses of all blocks in this function."""
        result = []
        cb = ffi.callback('void(const dragon_block_t *, void *)',
                          lambda ptr, _: result.append(
                              lib.dragon_block_start_address(ptr)))
        lib.dragon_function_each_block(self._ptr, cb, ffi.NULL)
        return result

    def __repr__(self):
        return f'<Function 0x{self.start_address:x} valid={self.is_valid}>'


# -- Graph ---------------------------------------------------------------------

class Graph:
    """Owns the instruction listing and address queues."""

    def __init__(self, ptr):
        self._ptr = ptr

    def __del__(self):
        if self._ptr:
            lib.dragon_graph_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    def __len__(self):
        return lib.dragon_graph_size(self._ptr)

    def __bool__(self):
        return not lib.dragon_graph_empty(self._ptr)

    def get_instruction(self, address):
        ptr = lib.dragon_graph_get_instruction(self._ptr, address)
        if ptr == ffi.NULL:
            return None
        return Instruction(ptr)

    def get_block(self, address):
        return BasicBlock(lib.dragon_graph_get_block(self._ptr, address))

    def get_function(self, address):
        return Function(lib.dragon_graph_get_function(self._ptr, address))

    def instructions(self):
        result = []
        cb = ffi.callback('void(const dragon_instruction_t *, void *)',
                          lambda ptr, _: result.append(Instruction(ptr)))
        lib.dragon_graph_each_instruction(self._ptr, cb, ffi.NULL)
        return result

    def block_addresses(self):
        result = []
        cb = ffi.callback('void(uint64_t, void *)', lambda addr, _: result.append(addr))
        lib.dragon_graph_each_block_address(self._ptr, cb, ffi.NULL)
        return result

    def function_addresses(self):
        result = []
        cb = ffi.callback('void(uint64_t, void *)', lambda addr, _: result.append(addr))
        lib.dragon_graph_each_function_address(self._ptr, cb, ffi.NULL)
        return result

    def get_ignil_block(self, address):
        """Return the IGNILBlock for *address*, or None if not lifted yet."""
        ptr = lib.dragon_graph_get_ignil_block(self._ptr, address)
        if ptr == ffi.NULL:
            return None
        return IGNILBlock(ptr)

    def __repr__(self):
        return f'<Graph instructions={len(self)}>'


# -- Configuration -------------------------------------------------------------

class Configuration:
    """Holds syntax, disassembly strategy, and LLVM target knobs.

    Parameters
    ----------
    syntax : Syntax
        Assembly syntax used for printing (INTEL or ATT).
    strategy : Strategy
        Disassembly algorithm (LINEAR_SWEEP or RECURSIVE_TRAVERSAL).
    cpu : str
        LLVM CPU name forwarded to MCSubtargetInfo. An empty string selects
        the "generic" baseline for the target triple, which may omit optional
        ISA extensions. Examples:
            "cortex-a8"   -- ARMv7-A with Thumb2 and NEON
            "arm926ej-s"  -- ARMv5TEJ, enables BX/BLX on ARM32
            "skylake"     -- x86-64 with AVX2, BMI2, etc.
            "sifive-u74"  -- RISC-V 64 with M/A/C extensions
    features : str
        LLVM feature string forwarded to MCSubtargetInfo. Comma-separated
        list of features, each prefixed with '+' (enable) or '-' (disable).
        Combined with those implied by `cpu`. Examples:
            "+v4t"        -- enable ARMv4T on ARM32 (adds BX instruction)
            "+v7,+neon"   -- ARMv7 with NEON
            "+avx2"       -- enable AVX2 on x86
            "+m,+a,+c"    -- RISC-V multiply, atomics, compressed
    """

    def __init__(self, syntax=Syntax.INTEL, strategy=Strategy.LINEAR_SWEEP,
                 cpu='', features='', logging=False):
        self._ptr = lib.dragon_config_create(syntax, strategy)
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create configuration')
        if cpu:
            lib.dragon_config_set_cpu(self._ptr, cpu.encode())
        if features:
            lib.dragon_config_set_features(self._ptr, features.encode())
        if logging:
            lib.dragon_config_set_logging(self._ptr, 1)

    def __del__(self):
        if self._ptr:
            lib.dragon_config_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def syntax(self):
        return lib.dragon_config_syntax(self._ptr)

    @property
    def strategy(self):
        return lib.dragon_config_strategy(self._ptr)

    @property
    def cpu(self):
        """LLVM CPU name currently set (empty string means 'generic')."""
        return ffi.string(lib.dragon_config_cpu(self._ptr)).decode()

    @cpu.setter
    def cpu(self, value):
        lib.dragon_config_set_cpu(self._ptr, value.encode())

    @property
    def features(self):
        """LLVM feature string currently set."""
        return ffi.string(lib.dragon_config_features(self._ptr)).decode()

    @features.setter
    def features(self, value):
        lib.dragon_config_set_features(self._ptr, value.encode())

    @property
    def logging(self):
        """Whether diagnostic logging to stderr is enabled."""
        return bool(lib.dragon_config_logging(self._ptr))

    @logging.setter
    def logging(self, value):
        lib.dragon_config_set_logging(self._ptr, 1 if value else 0)

    def __repr__(self):
        cpu_part = f' cpu={self.cpu!r}' if self.cpu else ''
        feat_part = f' features={self.features!r}' if self.features else ''
        log_part = ' logging=True' if self.logging else ''
        return f'<Configuration syntax={self.syntax} strategy={self.strategy}{cpu_part}{feat_part}{log_part}>'


# -- Disassembler --------------------------------------------------------------

class Disassembler:
    """Disassembles raw bytes into a Graph given an architecture and configuration."""

    def __init__(self, arch=Architecture.X86_64, config=None):
        if config is None:
            config = Configuration()
        self._config = config
        self._ptr = lib.dragon_disasm_create(arch, config._ptr)
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create disassembler')

    def __del__(self):
        if self._ptr:
            lib.dragon_disasm_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def is_valid(self):
        return bool(lib.dragon_disasm_valid(self._ptr))

    def disassemble(self, bytes_data, base_address=0x0):
        """Disassemble raw bytes and return a Graph. Raises RuntimeError on failure."""
        buf = ffi.from_buffer(bytes_data)
        ptr = lib.dragon_disasm_disassemble(self._ptr, buf, len(bytes_data), base_address)
        if ptr == ffi.NULL:
            raise RuntimeError('disassembly failed — unsupported architecture or invalid input')
        return Graph(ptr)

    def analyze(self, bytes_data, base_address=0x0):
        """Disassemble and run architecture-specific analysis (block and function
        discovery). Returns a fully populated Graph. Raises RuntimeError on failure."""
        buf = ffi.from_buffer(bytes_data)
        ptr = lib.dragon_disasm_analyze(self._ptr, buf, len(bytes_data), base_address)
        if ptr == ffi.NULL:
            raise RuntimeError('analysis failed — unsupported architecture or invalid input')
        return Graph(ptr)

    def lift_trace(self, instructions):
        """Lift an arbitrary linear sequence of already-decoded instructions
        straight to IGNIL, bypassing CFG/function/block validity analysis —
        e.g. a captured execution trace (Unicorn/QBDI/Frida/Intel PT) that
        isn't claimed to form a single valid basic block or a coherent CFG.

        Parameters
        ----------
        instructions : list[Instruction]
            Consulted in the order given — they do not need to be contiguous
            or even from the same Graph, as long as they were decoded for
            this Disassembler's architecture. Typically the result of
            Graph.instructions() or repeated Graph.get_instruction() calls.

        Returns
        -------
        IGNILBlock
            An owned block (unlike Graph.get_ignil_block()'s borrowed one) —
            use as a context manager or let garbage collection destroy it.
            Empty for RISC-V (no IGNIL lifter yet) or an empty trace.
        """
        ptrs = ffi.new('const dragon_instruction_t *[]',
                       [insn._ptr for insn in instructions]) if instructions else ffi.NULL
        ptr = lib.dragon_disasm_lift_trace(self._ptr, ptrs, len(instructions))
        if ptr == ffi.NULL:
            raise RuntimeError('lift_trace failed')
        return IGNILBlock(ptr, owned=True)

    def __repr__(self):
        return f'<Disassembler valid={self.is_valid}>'


# -- Assembler -----------------------------------------------------------------

class Assembler:
    """Assembles source text to raw bytes."""

    def __init__(self, arch=Architecture.X86_64, config=None):
        if config is None:
            config = Configuration()
        self._config = config
        self._ptr = lib.dragon_asm_create(arch, config._ptr)
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create assembler')

    def __del__(self):
        if self._ptr:
            lib.dragon_asm_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def is_valid(self):
        return bool(lib.dragon_asm_valid(self._ptr))

    def assemble(self, source):
        """Assemble source text and return bytes. Raises RuntimeError on failure."""
        out_len = ffi.new('size_t *')
        ptr = lib.dragon_asm_assemble(self._ptr, source.encode(), out_len)
        if ptr == ffi.NULL:
            raise RuntimeError('assembly failed')
        try:
            return bytes(ffi.buffer(ptr, out_len[0]))
        finally:
            lib.dragon_bytes_free(ptr)

    def __repr__(self):
        return f'<Assembler valid={self.is_valid}>'


# -- IGNIL opcode names --------------------------------------------------------

class IGNILOpcode:
    """Mirror of dragon_ignil_opcode_t.  All values match the C enum."""
    COPY  = 0;  LOAD  = 1;  STORE = 2
    ADD   = 3;  SUB   = 4;  NEG   = 5;  MUL   = 6
    SDIV  = 7;  UDIV  = 8;  SMOD  = 9;  UMOD  = 10
    AND   = 11; OR    = 12; XOR   = 13; NOT   = 14
    SHL   = 15; SHR   = 16; SAR   = 17
    ZEXT  = 18; SEXT  = 19; TRUNC = 20
    EQ    = 21; NEQ   = 22
    ULT   = 23; ULE   = 24; UGT   = 25; UGE   = 26
    SLT   = 27; SLE   = 28; SGT   = 29; SGE   = 30
    JMP   = 31; CJMP  = 32; CALL  = 33; RET   = 34
    UNDEF = 35
    # Appended after UNDEF, not interleaved with ADD/SUB/MUL above where
    # they'd read more naturally - matches dragon_c.h's dragon_ignil_opcode_t.
    FADD = 36; FSUB = 37; FMUL = 38; FDIV = 39
    SITOFP = 40; FPTOSI = 41

    @staticmethod
    def name(opcode_int):
        """Return the mnemonic string for an opcode integer."""
        return ffi.string(lib.dragon_ignil_opcode_name(opcode_int)).decode()


# -- IGNIL block ---------------------------------------------------------------

class IGNILBlock:
    """View of an IGNIL block.

    Blocks obtained from Graph.get_ignil_block() are borrowed — valid while
    the owning Graph is alive, and must not be destroyed. Blocks obtained
    from Disassembler.lift_trace() are owned by this object instead (there is
    no backing Graph) and are destroyed when it is garbage-collected / the
    context manager exits.
    """

    def __init__(self, ptr, owned=False):
        self._ptr = ptr
        self._owned = owned

    def __del__(self):
        if self._owned and self._ptr:
            lib.dragon_ignil_block_destroy(self._ptr)
        self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def address(self):
        return lib.dragon_ignil_block_address(self._ptr)

    @property
    def size(self):
        return lib.dragon_ignil_block_size(self._ptr)

    def to_string(self):
        """Return a multi-line text rendering of all ops in this block."""
        raw = lib.dragon_ignil_block_to_string(self._ptr)
        try:
            return ffi.string(raw).decode()
        finally:
            lib.dragon_cstr_free(raw)

    def op_to_string(self, index):
        """Return the text rendering of the op at *index* (0-based)."""
        raw = lib.dragon_ignil_block_op_to_string(self._ptr, index)
        if raw == ffi.NULL:
            return None
        try:
            return ffi.string(raw).decode()
        finally:
            lib.dragon_cstr_free(raw)

    def ops(self):
        """Return a list of (op_addr, opcode_int, index) tuples for all ops."""
        result = []
        cb = ffi.callback(
            'void(uint64_t, dragon_ignil_opcode_t, size_t, void *)',
            lambda addr, opc, idx, _: result.append((addr, int(opc), idx))
        )
        lib.dragon_ignil_block_each_op(self._ptr, cb, ffi.NULL)
        return result

    def simplify(self, max_iterations=0):
        """Peephole-simplify this block to a fixed point: constant
        folding/propagation, algebraic identity/annihilator elimination,
        no-op width-conversion elimination, and dead-Temp elimination.
        Block-local only — never crosses into predecessor/successor blocks,
        and never removes a Reg-defining op (a Reg may be live-out).

        Parameters
        ----------
        max_iterations : int
            Upper bound on fixed-point passes. 0 selects the library
            default (8).

        Returns
        -------
        (IGNILBlock, SimplifyStats)
            The simplified block is a new, independently owned IGNILBlock —
            this block is left untouched.
        """
        out_cf = ffi.new('size_t *')
        out_ia = ffi.new('size_t *')
        out_or = ffi.new('size_t *')
        out_it = ffi.new('size_t *')
        ptr = lib.dragon_ignil_simplify_block(
            self._ptr, max_iterations, out_cf, out_ia, out_or, out_it)
        stats = SimplifyStats(out_cf[0], out_ia[0], out_or[0], out_it[0])
        return IGNILBlock(ptr, owned=True), stats

    def __repr__(self):
        return f'<IGNILBlock 0x{self.address:x} ops={self.size}>'


class SimplifyStats:
    """Counts from one IGNILBlock.simplify() call. Mirrors ignil::SimplifyStats."""

    def __init__(self, constants_folded, identities_applied, ops_removed, iterations):
        self.constants_folded = constants_folded
        self.identities_applied = identities_applied
        self.ops_removed = ops_removed
        self.iterations = iterations

    @property
    def changed(self):
        return bool(self.constants_folded or self.identities_applied or self.ops_removed)

    def __repr__(self):
        return (f'<SimplifyStats constants_folded={self.constants_folded} '
                f'identities_applied={self.identities_applied} '
                f'ops_removed={self.ops_removed} iterations={self.iterations}>')


# -- SliceExpr -----------------------------------------------------------------

class SliceExprType:
    """Mirror of dragon_slice_type_t."""
    CONST   = 0
    SYM_REG = 1
    BINOP   = 2
    UNARYOP = 3
    LOAD    = 4
    UNDEF   = 5


class SliceExpr:
    """Owned handle to a SliceExpr tree node.  Must be destroyed after use.
    Child nodes returned by left(), right(), operand(), load_addr() are also
    owned SliceExpr objects that must be destroyed independently."""

    def __init__(self, ptr):
        self._ptr = ptr

    def __del__(self):
        if self._ptr:
            lib.dragon_slice_expr_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    @property
    def type(self):
        """Return a SliceExprType constant."""
        return int(lib.dragon_slice_expr_type(self._ptr))

    def to_string(self):
        """Human-readable rendering of this expression tree."""
        raw = lib.dragon_slice_expr_to_string(self._ptr)
        try:
            return ffi.string(raw).decode()
        finally:
            lib.dragon_cstr_free(raw)

    def to_z3_string(self):
        """Lower this expression through Z3 and return the Z3 bitvector
        expression as a string.
        Raises RuntimeError if the library was built without Z3 support."""
        try:
            raw = lib.dragon_slice_expr_to_z3_string(self._ptr)
        except AttributeError:
            raise RuntimeError('dragon was built without Z3 support (-DDRAGON_USE_Z3=ON required)')
        try:
            return ffi.string(raw).decode()
        finally:
            lib.dragon_cstr_free(raw)

    def enumerate_values(self, max_values=0, sorted=True):
        """Enumerate concrete 64-bit values this expression can take, via Z3.

        Parameters
        ----------
        max_values : int
            Upper bound on results returned. 0 selects the library default (256).
        sorted : bool
            Sort results ascending (library default: True).

        Note: constructs its own Z3 context internally — there is no way to
        pass extra Z3 assertions across this boundary, unlike the C++ API's
        optional `constraints` parameter; use the C++ API directly if you
        need that.

        Raises RuntimeError if the library was built without Z3 support."""
        out_count = ffi.new('size_t *')
        try:
            ptr = lib.dragon_slice_expr_enumerate_values(
                self._ptr, max_values, 1 if sorted else 0, out_count)
        except AttributeError:
            raise RuntimeError('dragon was built without Z3 support (-DDRAGON_USE_Z3=ON required)')
        try:
            return [ptr[i] for i in range(out_count[0])]
        finally:
            lib.dragon_u64_array_free(ptr)

    def solve_unique(self):
        """Solve for a single concrete value this expression must take, via Z3.
        Returns None if zero or more than one model exists (unsatisfiable or
        underconstrained), the value itself if exactly one does.

        Raises RuntimeError if the library was built without Z3 support."""
        out_value = ffi.new('uint64_t *')
        try:
            found = lib.dragon_slice_expr_solve_unique(self._ptr, out_value)
        except AttributeError:
            raise RuntimeError('dragon was built without Z3 support (-DDRAGON_USE_Z3=ON required)')
        return out_value[0] if found else None

    # -- CONST

    @property
    def const_value(self):
        return lib.dragon_slice_expr_const_value(self._ptr)

    @property
    def const_width(self):
        return lib.dragon_slice_expr_const_width(self._ptr)

    # -- SYM_REG

    @property
    def symreg_offset(self):
        return lib.dragon_slice_expr_symreg_offset(self._ptr)

    @property
    def symreg_width(self):
        return lib.dragon_slice_expr_symreg_width(self._ptr)

    @property
    def symreg_name(self):
        """Register name if one was set, empty string otherwise."""
        return ffi.string(lib.dragon_slice_expr_symreg_name(self._ptr)).decode()

    # -- BINOP / UNARYOP

    @property
    def opcode(self):
        """Return the IGNILOpcode integer for BINOP or UNARYOP nodes."""
        return int(lib.dragon_slice_expr_opcode(self._ptr))

    @property
    def opcode_name(self):
        """Return the mnemonic string for the opcode."""
        return IGNILOpcode.name(self.opcode)

    def left(self):
        """Return the left child of a BINOP node (owned — caller must close/del)."""
        ptr = lib.dragon_slice_expr_left(self._ptr)
        return SliceExpr(ptr) if ptr != ffi.NULL else None

    def right(self):
        """Return the right child of a BINOP node (owned — caller must close/del)."""
        ptr = lib.dragon_slice_expr_right(self._ptr)
        return SliceExpr(ptr) if ptr != ffi.NULL else None

    def operand(self):
        """Return the child of a UNARYOP node (owned — caller must close/del)."""
        ptr = lib.dragon_slice_expr_operand(self._ptr)
        return SliceExpr(ptr) if ptr != ffi.NULL else None

    # -- LOAD

    def load_addr(self):
        """Return the address sub-expression of a LOAD node (owned)."""
        ptr = lib.dragon_slice_expr_load_addr(self._ptr)
        return SliceExpr(ptr) if ptr != ffi.NULL else None

    @property
    def load_width(self):
        return lib.dragon_slice_expr_load_width(self._ptr)

    def __repr__(self):
        return f'<SliceExpr {self.to_string()}>'


# -- BackwardSlicer ------------------------------------------------------------

class BackwardSlicer:
    """Slices register values backwards through an IGNIL CFG.

    Parameters
    ----------
    max_blocks : int
        Maximum number of inter-block steps (default 5).
        The slicer stops at join nodes regardless of this limit.
    """

    def __init__(self, max_blocks=5):
        self._ptr = lib.dragon_slicer_create(max_blocks)
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create BackwardSlicer')

    def __del__(self):
        if self._ptr:
            lib.dragon_slicer_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    def slice_reg(self, graph, block_addr, reg_offset, reg_width, reg_name=''):
        """Slice a register backwards through the CFG of *graph*.

        Parameters
        ----------
        graph      : Graph
            A fully lifted graph (produced by Disassembler.analyze).
        block_addr : int
            Address of the block where slicing starts.
        reg_offset : int
            Byte offset of the register in the IGNIL register space.
        reg_width  : int
            Bit width of the register.
        reg_name   : str
            Optional human-readable name carried into SymReg results when
            the value cannot be resolved.  Pass via ``*x86.RAX`` to have
            names filled in automatically.

        Returns
        -------
        SliceExpr
            Owned expression tree (caller must destroy / use as context manager).
        """
        name_bytes = reg_name.encode() if reg_name else ffi.NULL
        ptr = lib.dragon_slicer_slice_reg(
            self._ptr, graph._ptr, block_addr, reg_offset, reg_width,
            name_bytes)
        if ptr == ffi.NULL:
            raise RuntimeError('slicer returned NULL')
        return SliceExpr(ptr)

    def __repr__(self):
        return '<BackwardSlicer>'


# -- SwitchDetector --------------------------------------------------------------

class SwitchCandidate:
    """Everything extracted from a recognized jump-table dispatch pattern.
    Mirrors dragon_switch_candidate_t — construct via detect_possible_switch(),
    not directly.

    index_reg_offset/index_reg_width identify the index register in the
    IGNIL register address space (see x86/arm register descriptors below),
    the same convention BackwardSlicer.slice_reg() uses.
    """

    def __init__(self, table_base, entry_size, num_cases,
                 index_reg_offset, index_reg_width):
        self.table_base = table_base
        self.entry_size = entry_size
        self.num_cases = num_cases
        self.index_reg_offset = index_reg_offset
        self.index_reg_width = index_reg_width

    def __repr__(self):
        return (f'<SwitchCandidate table_base=0x{self.table_base:x} '
                f'entry_size={self.entry_size} num_cases={self.num_cases}>')


def detect_possible_switch(jump_target, block_addr, graph):
    """Inspect a backward-slice result (typically from
    BackwardSlicer.slice_reg() on an indirect jump's target) and determine
    whether it is a jump-table indirect branch.

    Parameters
    ----------
    jump_target : SliceExpr
    block_addr  : int
        Address of the block containing the indirect jump.
    graph       : Graph

    Returns
    -------
    SwitchCandidate or None
        None if the pattern didn't match. A returned candidate with
        num_cases == 0 means the pattern matched but the bounds guard
        wasn't found.
    """
    out = ffi.new('dragon_switch_candidate_t *')
    matched = lib.dragon_detect_possible_switch(
        jump_target._ptr, block_addr, graph._ptr, out)
    if not matched:
        return None
    return SwitchCandidate(out.table_base, out.entry_size, out.num_cases,
                            out.index_reg_offset, out.index_reg_width)


def read_table_targets(candidate, data, base_addr):
    """Read candidate.num_cases pointer-sized little-endian entries from the
    jump table *candidate* describes.

    Parameters
    ----------
    candidate : SwitchCandidate
    data      : bytes-like
        Buffer starting at *base_addr* in the binary's address space (not
        necessarily starting at candidate.table_base — the table is
        located within this window using its own address).
    base_addr : int
        Address the first byte of *data* corresponds to.

    Returns
    -------
    list[int]
        Empty if num_cases == 0 or the table extends outside *data*.
    """
    buf = ffi.from_buffer(data)
    out_count = ffi.new('size_t *')
    sw = ffi.new('dragon_switch_candidate_t *')
    sw.table_base = candidate.table_base
    sw.entry_size = candidate.entry_size
    sw.num_cases = candidate.num_cases
    sw.index_reg_offset = candidate.index_reg_offset
    sw.index_reg_width = candidate.index_reg_width
    ptr = lib.dragon_read_table_targets(sw, buf, len(data), base_addr, out_count)
    try:
        return [ptr[i] for i in range(out_count[0])]
    finally:
        lib.dragon_u64_array_free(ptr)


# -- LLVMLifter ----------------------------------------------------------------

class LLVMLifter:
    """Lifts Functions to LLVM IR via the IGNIL graph.

    Parameters
    ----------
    arch        : Architecture
        Target architecture (default X86_64).
    module_name : str
        Name embedded in the LLVM module (default 'lifted').
    """

    def __init__(self, arch=Architecture.X86_64, module_name='lifted'):
        self._ptr = lib.dragon_llvm_lifter_create(arch, module_name.encode())
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create LLVMLifter')

    def __del__(self):
        if self._ptr:
            lib.dragon_llvm_lifter_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    def lift(self, function, graph):
        """Lift *function* using its IGNIL blocks in *graph*.

        Returns True on success; False if any IGNIL block is missing.
        """
        return bool(lib.dragon_llvm_lifter_lift(
            self._ptr, function._ptr, graph._ptr))

    def ir(self):
        """Return the textual LLVM IR for all lifted functions."""
        raw = lib.dragon_llvm_lifter_ir(self._ptr)
        try:
            return ffi.string(raw).decode()
        finally:
            lib.dragon_cstr_free(raw)

    def run_mem2reg(self):
        """Promote allocas to SSA values."""
        lib.dragon_llvm_lifter_run_mem2reg(self._ptr)

    def run_inst_combine(self):
        """Fold sub-register mask/OR patterns."""
        lib.dragon_llvm_lifter_run_inst_combine(self._ptr)

    def run_gvn(self):
        """Eliminate redundant loads."""
        lib.dragon_llvm_lifter_run_gvn(self._ptr)

    def run_dce(self):
        """Remove dead flag computations."""
        lib.dragon_llvm_lifter_run_dce(self._ptr)

    def optimize(self):
        """Run the standard pipeline: mem2reg → instcombine → GVN → DCE."""
        self.run_mem2reg()
        self.run_inst_combine()
        self.run_gvn()
        self.run_dce()

    def __repr__(self):
        return '<LLVMLifter>'


# -- FLIRT signatures (dragon::sigs) --------------------------------------------

def sigs_crc16(data):
    """Compute dragon-tales' signature CRC16 (reflected, poly 0xA001, init
    0xFFFF, no final XOR — CRC-16/ARC) over *data*.

    Caveat: this variant was never verified against a real IDA/FLAIR-generated
    .pat file — see SignatureDB's class doc.
    """
    buf = ffi.from_buffer(data)
    return lib.dragon_sigs_crc16(buf, len(data))


class SignatureDB:
    """A loaded collection of FLIRT .pat signatures, ready for matching."""

    def __init__(self):
        self._ptr = lib.dragon_sigdb_create()
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create SignatureDB')

    def __del__(self):
        if self._ptr:
            lib.dragon_sigdb_destroy(self._ptr)
            self._ptr = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.__del__()

    def load_directory(self, path):
        """Load and append every .pat file directly inside *path* (not
        recursive). Files that fail to parse are silently skipped — compare
        signature_count() before/after to see how much was actually loaded."""
        lib.dragon_sigdb_load_directory(self._ptr, str(path).encode())

    def load_file(self, path):
        """Load and append a single .pat file. No-op if it can't be parsed."""
        lib.dragon_sigdb_load_file(self._ptr, str(path).encode())

    @property
    def signature_count(self):
        return lib.dragon_sigdb_signature_count(self._ptr)

    def match_at(self, data, address):
        """Try every signature against *data*, which must begin at the
        candidate function's address. Returns the first match as a
        (address, name, length) tuple, or None if nothing matched."""
        buf = ffi.from_buffer(data)
        out_address = ffi.new('uint64_t *')
        out_name = ffi.new('char **')
        out_length = ffi.new('uint32_t *')
        matched = lib.dragon_sigdb_match_at(
            self._ptr, buf, len(data), address,
            out_address, out_name, out_length)
        if not matched:
            return None
        try:
            name = ffi.string(out_name[0]).decode()
        finally:
            lib.dragon_cstr_free(out_name[0])
        return (out_address[0], name, out_length[0])

    def match_entries(self, image, image_base, entry_points):
        """Try every address in *entry_points* against every signature.
        *image*/*image_base* describe the byte region the entry points fall
        within. Unlike match_at, every name a matching signature defines is
        reported (a module can bundle more than one function/offset under
        one pattern), so one entry point may produce more than one match.

        Returns a list of (address, name, length) tuples.
        """
        buf = ffi.from_buffer(image)
        entries = ffi.new('uint64_t[]', entry_points) if entry_points else ffi.NULL
        result = []
        cb = ffi.callback(
            'void(uint64_t, const char *, uint32_t, void *)',
            lambda addr, name, length, _: result.append(
                (addr, ffi.string(name).decode(), length))
        )
        lib.dragon_sigdb_match_entries(
            self._ptr, buf, len(image), image_base,
            entries, len(entry_points), cb, ffi.NULL)
        return result

    def __repr__(self):
        return f'<SignatureDB signatures={self.signature_count}>'


# -- x86 register descriptors --------------------------------------------------

class x86:
    """x86 / x86-64 IGNIL register descriptors as (offset, width, name) tuples.

    Usage::

        slicer.slice_reg(graph, block_addr, *x86.RAX)
        # equivalent to: slice_reg(graph, block_addr, 0, 64, 'RAX')

    The name is carried into SymReg results and printed in to_string() output
    when the slicer cannot resolve the register to a concrete value.

    Layout (byte offsets in the IGNIL register address space):
        RAX=0  RCX=8  RDX=16  RBX=24  RSP=32  RBP=40  RSI=48  RDI=56
        R8=64 ... R15=120  RIP=128
        CF=136  PF=137  AF=138  ZF=139  SF=140  OF=141  DF=142
    """

    # 64-bit GP registers
    RAX = (0,   64, 'RAX'); RCX = (8,   64, 'RCX'); RDX = (16,  64, 'RDX'); RBX = (24,  64, 'RBX')
    RSP = (32,  64, 'RSP'); RBP = (40,  64, 'RBP'); RSI = (48,  64, 'RSI'); RDI = (56,  64, 'RDI')
    R8  = (64,  64, 'R8');  R9  = (72,  64, 'R9');  R10 = (80,  64, 'R10'); R11 = (88,  64, 'R11')
    R12 = (96,  64, 'R12'); R13 = (104, 64, 'R13'); R14 = (112, 64, 'R14'); R15 = (120, 64, 'R15')
    RIP = (128, 64, 'RIP')

    # 32-bit GP registers
    EAX  = (0,   32, 'EAX');  ECX  = (8,   32, 'ECX');  EDX  = (16,  32, 'EDX');  EBX  = (24,  32, 'EBX')
    ESP  = (32,  32, 'ESP');  EBP  = (40,  32, 'EBP');  ESI  = (48,  32, 'ESI');  EDI  = (56,  32, 'EDI')
    R8D  = (64,  32, 'R8D');  R9D  = (72,  32, 'R9D');  R10D = (80,  32, 'R10D'); R11D = (88,  32, 'R11D')
    R12D = (96,  32, 'R12D'); R13D = (104, 32, 'R13D'); R14D = (112, 32, 'R14D'); R15D = (120, 32, 'R15D')

    # 16-bit GP registers
    AX = (0,  16, 'AX'); CX = (8,  16, 'CX'); DX = (16, 16, 'DX'); BX = (24, 16, 'BX')
    SP = (32, 16, 'SP'); BP = (40, 16, 'BP'); SI = (48, 16, 'SI'); DI = (56, 16, 'DI')

    # 8-bit low GP registers
    AL  = (0,  8, 'AL');  CL  = (8,  8, 'CL');  DL  = (16, 8, 'DL');  BL  = (24, 8, 'BL')
    SPL = (32, 8, 'SPL'); BPL = (40, 8, 'BPL'); SIL = (48, 8, 'SIL'); DIL = (56, 8, 'DIL')
    # 8-bit high registers (legacy aliases)
    AH = (1,  8, 'AH'); CH = (9,  8, 'CH'); DH = (17, 8, 'DH'); BH = (25, 8, 'BH')

    # EFLAGS — 8-bit slots in the IGNIL register space (x86registers.def uses width=8)
    CF = (136, 8, 'CF'); PF = (137, 8, 'PF'); AF = (138, 8, 'AF'); ZF = (139, 8, 'ZF')
    SF = (140, 8, 'SF'); OF = (141, 8, 'OF'); DF = (142, 8, 'DF')


# -- AArch64 register descriptors -----------------------------------------------

class arm:
    """AArch64 IGNIL register descriptors as (offset, width, name) tuples.

    Usage::

        slicer.slice_reg(graph, block_addr, *arm.X0)
        # equivalent to: slice_reg(graph, block_addr, 0, 64, 'X0')

    The name is carried into SymReg results and printed in to_string() output
    when the slicer cannot resolve the register to a concrete value.

    Layout (byte offsets in the IGNIL register address space):
        X0=0  X1=8  X2=16 ... X28=224  X29(FP)=232  X30(LR)=240
        XZR=248  SP=256  PC=264
        V0..V31=272..768 (16 bytes each)
        NF=784  ZF=785  CF=786  VF=787
    """

    # 64-bit general-purpose registers
    X0  = (0,   64, 'X0');  X1  = (8,   64, 'X1');  X2  = (16,  64, 'X2');  X3  = (24,  64, 'X3')
    X4  = (32,  64, 'X4');  X5  = (40,  64, 'X5');  X6  = (48,  64, 'X6');  X7  = (56,  64, 'X7')
    X8  = (64,  64, 'X8');  X9  = (72,  64, 'X9');  X10 = (80,  64, 'X10'); X11 = (88,  64, 'X11')
    X12 = (96,  64, 'X12'); X13 = (104, 64, 'X13'); X14 = (112, 64, 'X14'); X15 = (120, 64, 'X15')
    X16 = (128, 64, 'X16'); X17 = (136, 64, 'X17'); X18 = (144, 64, 'X18'); X19 = (152, 64, 'X19')
    X20 = (160, 64, 'X20'); X21 = (168, 64, 'X21'); X22 = (176, 64, 'X22'); X23 = (184, 64, 'X23')
    X24 = (192, 64, 'X24'); X25 = (200, 64, 'X25'); X26 = (208, 64, 'X26'); X27 = (216, 64, 'X27')
    X28 = (224, 64, 'X28'); X29 = (232, 64, 'X29'); X30 = (240, 64, 'X30'); XZR = (248, 64, 'XZR')
    SP  = (256, 64, 'SP');  PC  = (264, 64, 'PC')

    # Aliases for the frame pointer / link register
    FP = (232, 64, 'FP'); LR = (240, 64, 'LR')

    # 32-bit views (lower half of each X register)
    W0  = (0,   32, 'W0');  W1  = (8,   32, 'W1');  W2  = (16,  32, 'W2');  W3  = (24,  32, 'W3')
    W4  = (32,  32, 'W4');  W5  = (40,  32, 'W5');  W6  = (48,  32, 'W6');  W7  = (56,  32, 'W7')
    W8  = (64,  32, 'W8');  W9  = (72,  32, 'W9');  W10 = (80,  32, 'W10'); W11 = (88,  32, 'W11')
    W12 = (96,  32, 'W12'); W13 = (104, 32, 'W13'); W14 = (112, 32, 'W14'); W15 = (120, 32, 'W15')
    W16 = (128, 32, 'W16'); W17 = (136, 32, 'W17'); W18 = (144, 32, 'W18'); W19 = (152, 32, 'W19')
    W20 = (160, 32, 'W20'); W21 = (168, 32, 'W21'); W22 = (176, 32, 'W22'); W23 = (184, 32, 'W23')
    W24 = (192, 32, 'W24'); W25 = (200, 32, 'W25'); W26 = (208, 32, 'W26'); W27 = (216, 32, 'W27')
    W28 = (224, 32, 'W28'); W29 = (232, 32, 'W29'); W30 = (240, 32, 'W30'); WZR = (248, 32, 'WZR')
    WSP = (256, 32, 'WSP')

    # NEON/FP vector registers — V/Q share the 128-bit slot; D/S/H/B select
    # progressively smaller slices of the same offset.
    V0  = (272,  128, 'V0');  V1  = (288,  128, 'V1');  V2  = (304,  128, 'V2');  V3  = (320,  128, 'V3')
    V4  = (336,  128, 'V4');  V5  = (352,  128, 'V5');  V6  = (368,  128, 'V6');  V7  = (384,  128, 'V7')
    V8  = (400,  128, 'V8');  V9  = (416,  128, 'V9');  V10 = (432,  128, 'V10'); V11 = (448,  128, 'V11')
    V12 = (464,  128, 'V12'); V13 = (480,  128, 'V13'); V14 = (496,  128, 'V14'); V15 = (512,  128, 'V15')
    V16 = (528,  128, 'V16'); V17 = (544,  128, 'V17'); V18 = (560,  128, 'V18'); V19 = (576,  128, 'V19')
    V20 = (592,  128, 'V20'); V21 = (608,  128, 'V21'); V22 = (624,  128, 'V22'); V23 = (640,  128, 'V23')
    V24 = (656,  128, 'V24'); V25 = (672,  128, 'V25'); V26 = (688,  128, 'V26'); V27 = (704,  128, 'V27')
    V28 = (720,  128, 'V28'); V29 = (736,  128, 'V29'); V30 = (752,  128, 'V30'); V31 = (768,  128, 'V31')

    Q0  = (272,  128, 'Q0');  Q1  = (288,  128, 'Q1');  Q2  = (304,  128, 'Q2');  Q3  = (320,  128, 'Q3')
    Q4  = (336,  128, 'Q4');  Q5  = (352,  128, 'Q5');  Q6  = (368,  128, 'Q6');  Q7  = (384,  128, 'Q7')
    Q8  = (400,  128, 'Q8');  Q9  = (416,  128, 'Q9');  Q10 = (432,  128, 'Q10'); Q11 = (448,  128, 'Q11')
    Q12 = (464,  128, 'Q12'); Q13 = (480,  128, 'Q13'); Q14 = (496,  128, 'Q14'); Q15 = (512,  128, 'Q15')
    Q16 = (528,  128, 'Q16'); Q17 = (544,  128, 'Q17'); Q18 = (560,  128, 'Q18'); Q19 = (576,  128, 'Q19')
    Q20 = (592,  128, 'Q20'); Q21 = (608,  128, 'Q21'); Q22 = (624,  128, 'Q22'); Q23 = (640,  128, 'Q23')
    Q24 = (656,  128, 'Q24'); Q25 = (672,  128, 'Q25'); Q26 = (688,  128, 'Q26'); Q27 = (704,  128, 'Q27')
    Q28 = (720,  128, 'Q28'); Q29 = (736,  128, 'Q29'); Q30 = (752,  128, 'Q30'); Q31 = (768,  128, 'Q31')

    D0  = (272,  64, 'D0');  D1  = (288,  64, 'D1');  D2  = (304,  64, 'D2');  D3  = (320,  64, 'D3')
    D4  = (336,  64, 'D4');  D5  = (352,  64, 'D5');  D6  = (368,  64, 'D6');  D7  = (384,  64, 'D7')
    D8  = (400,  64, 'D8');  D9  = (416,  64, 'D9');  D10 = (432,  64, 'D10'); D11 = (448,  64, 'D11')
    D12 = (464,  64, 'D12'); D13 = (480,  64, 'D13'); D14 = (496,  64, 'D14'); D15 = (512,  64, 'D15')
    D16 = (528,  64, 'D16'); D17 = (544,  64, 'D17'); D18 = (560,  64, 'D18'); D19 = (576,  64, 'D19')
    D20 = (592,  64, 'D20'); D21 = (608,  64, 'D21'); D22 = (624,  64, 'D22'); D23 = (640,  64, 'D23')
    D24 = (656,  64, 'D24'); D25 = (672,  64, 'D25'); D26 = (688,  64, 'D26'); D27 = (704,  64, 'D27')
    D28 = (720,  64, 'D28'); D29 = (736,  64, 'D29'); D30 = (752,  64, 'D30'); D31 = (768,  64, 'D31')

    S0  = (272,  32, 'S0');  S1  = (288,  32, 'S1');  S2  = (304,  32, 'S2');  S3  = (320,  32, 'S3')
    S4  = (336,  32, 'S4');  S5  = (352,  32, 'S5');  S6  = (368,  32, 'S6');  S7  = (384,  32, 'S7')
    S8  = (400,  32, 'S8');  S9  = (416,  32, 'S9');  S10 = (432,  32, 'S10'); S11 = (448,  32, 'S11')
    S12 = (464,  32, 'S12'); S13 = (480,  32, 'S13'); S14 = (496,  32, 'S14'); S15 = (512,  32, 'S15')
    S16 = (528,  32, 'S16'); S17 = (544,  32, 'S17'); S18 = (560,  32, 'S18'); S19 = (576,  32, 'S19')
    S20 = (592,  32, 'S20'); S21 = (608,  32, 'S21'); S22 = (624,  32, 'S22'); S23 = (640,  32, 'S23')
    S24 = (656,  32, 'S24'); S25 = (672,  32, 'S25'); S26 = (688,  32, 'S26'); S27 = (704,  32, 'S27')
    S28 = (720,  32, 'S28'); S29 = (736,  32, 'S29'); S30 = (752,  32, 'S30'); S31 = (768,  32, 'S31')

    H0  = (272,  16, 'H0');  H1  = (288,  16, 'H1');  H2  = (304,  16, 'H2');  H3  = (320,  16, 'H3')
    H4  = (336,  16, 'H4');  H5  = (352,  16, 'H5');  H6  = (368,  16, 'H6');  H7  = (384,  16, 'H7')
    H8  = (400,  16, 'H8');  H9  = (416,  16, 'H9');  H10 = (432,  16, 'H10'); H11 = (448,  16, 'H11')
    H12 = (464,  16, 'H12'); H13 = (480,  16, 'H13'); H14 = (496,  16, 'H14'); H15 = (512,  16, 'H15')
    H16 = (528,  16, 'H16'); H17 = (544,  16, 'H17'); H18 = (560,  16, 'H18'); H19 = (576,  16, 'H19')
    H20 = (592,  16, 'H20'); H21 = (608,  16, 'H21'); H22 = (624,  16, 'H22'); H23 = (640,  16, 'H23')
    H24 = (656,  16, 'H24'); H25 = (672,  16, 'H25'); H26 = (688,  16, 'H26'); H27 = (704,  16, 'H27')
    H28 = (720,  16, 'H28'); H29 = (736,  16, 'H29'); H30 = (752,  16, 'H30'); H31 = (768,  16, 'H31')

    B0  = (272,  8, 'B0');  B1  = (288,  8, 'B1');  B2  = (304,  8, 'B2');  B3  = (320,  8, 'B3')
    B4  = (336,  8, 'B4');  B5  = (352,  8, 'B5');  B6  = (368,  8, 'B6');  B7  = (384,  8, 'B7')
    B8  = (400,  8, 'B8');  B9  = (416,  8, 'B9');  B10 = (432,  8, 'B10'); B11 = (448,  8, 'B11')
    B12 = (464,  8, 'B12'); B13 = (480,  8, 'B13'); B14 = (496,  8, 'B14'); B15 = (512,  8, 'B15')
    B16 = (528,  8, 'B16'); B17 = (544,  8, 'B17'); B18 = (560,  8, 'B18'); B19 = (576,  8, 'B19')
    B20 = (592,  8, 'B20'); B21 = (608,  8, 'B21'); B22 = (624,  8, 'B22'); B23 = (640,  8, 'B23')
    B24 = (656,  8, 'B24'); B25 = (672,  8, 'B25'); B26 = (688,  8, 'B26'); B27 = (704,  8, 'B27')
    B28 = (720,  8, 'B28'); B29 = (736,  8, 'B29'); B30 = (752,  8, 'B30'); B31 = (768,  8, 'B31')

    # NZCV condition flags — "VF" avoids collision with the V register prefix.
    NF = (784, 8, 'NF'); ZF = (785, 8, 'ZF'); CF = (786, 8, 'CF'); VF = (787, 8, 'VF')
