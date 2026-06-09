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

    @staticmethod
    def name(opcode_int):
        """Return the mnemonic string for an opcode integer."""
        return ffi.string(lib.dragon_ignil_opcode_name(opcode_int)).decode()


# -- IGNIL block ---------------------------------------------------------------

class IGNILBlock:
    """Borrowed view of an IGNIL block — valid while the owning Graph is alive.
    Do NOT destroy this object; its lifetime is managed by the Graph."""

    def __init__(self, ptr):
        self._ptr = ptr

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

    def __repr__(self):
        return f'<IGNILBlock 0x{self.address:x} ops={self.size}>'


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
