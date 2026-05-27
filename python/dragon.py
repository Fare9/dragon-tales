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
                 cpu='', features=''):
        self._ptr = lib.dragon_config_create(syntax, strategy)
        if self._ptr == ffi.NULL:
            raise RuntimeError('failed to create configuration')
        if cpu:
            lib.dragon_config_set_cpu(self._ptr, cpu.encode())
        if features:
            lib.dragon_config_set_features(self._ptr, features.encode())

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

    def __repr__(self):
        cpu_part = f' cpu={self.cpu!r}' if self.cpu else ''
        feat_part = f' features={self.features!r}' if self.features else ''
        return f'<Configuration syntax={self.syntax} strategy={self.strategy}{cpu_part}{feat_part}>'


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
