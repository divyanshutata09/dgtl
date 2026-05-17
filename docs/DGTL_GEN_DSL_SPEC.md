# DGTL-Gen DSL Specification v1.0

## Overview
DGTL-Gen is a Domain-Specific Language (DSL) for declaratively defining IR operations, types, attributes, traits, and interfaces. Similar to MLIR's TableGen, it generates production-ready C++ boilerplate: operation builders, getters, setters, validators, and metadata lookups.

## File Format
The `.dgtl_def` file uses a line-oriented YAML-inspired syntax:
- Comments: `#` to end of line
- Sections: `[section_name]` (optional grouping)
- Key-value pairs: `key: value` or `key=value`
- List values: pipe-separated `value1|value2|value3`
- Indentation: logical grouping (parsed line-by-line, not structurally)

## Section 1: Global Metadata

```
#@ version: 1.0
#@ namespace: dgtl::ir
#@ generated_by: dgtl_gen.ps1
```

## Section 2: Type Definitions

```
[types]
type I32: builtin, width=32
type I64: builtin, width=64
type F32: builtin, width=32
type F64: builtin, width=64
type VectorF32x16: vector, element_type=F32, width=512
type TensorI64: tensor, element_type=I64
```

**Type Structure:**
- `name`: Type identifier (C++ would use `Type::I32`)
- `category`: builtin | vector | tensor | pointer | function | custom
- `width`: Bit width (for primitives)
- `element_type`: Element type (for composite types)
- `layout`: Memory layout (row-major | col-major | blocked | custom)

## Section 3: Attribute Definitions

```
[attributes]
attr I32Attr: value_type=I32, default=0
attr StringAttr: storage=std::string
attr DictionaryAttr: storage=std::map<std::string, Attribute>
```

**Attribute Structure:**
- `name`: Attribute identifier
- `value_type`: Type of the attribute value
- `storage`: C++ storage type (if custom)
- `default`: Default value
- `verifier`: Custom verification function name

## Section 4: Trait Definitions

```
[traits]
trait Commutative: "Binary operations with commutative operands"
trait HasNoSideEffects: "Operation has no side effects"
trait IsTerminator: "Control flow terminator (branch, return, etc.)"
trait IsMemoryRead: "Reads from memory"
trait IsMemoryWrite: "Writes to memory"
trait IsMemoryOp: "Generic memory operation"
trait IsVectorizable: "Can be vectorized"
```

**Trait Structure:**
- `name`: Trait identifier
- `description`: Human-readable description

## Section 5: Interface Definitions

```
[interfaces]
interface Arithmetic: ops=(Add, Sub, Mul, Div, Mod)
interface Memory: ops=(Load, Store, Prefetch)
interface ControlFlow: ops=(Branch, BranchCond, Call, Return)
interface Terminator: ops=(Return, Branch)
interface SIMD: "SIMD-specific operations"
interface GPU: "GPU-specific operations"
interface Tensor: "Tensor/ML operations"
```

**Interface Structure:**
- `name`: Interface identifier
- `description`: Human-readable description
- `methods`: Implemented methods (optional)
- `ops`: Representative operations (optional)

## Section 6: Dialect Definitions

```
[dialects]
dialect core: namespace=dgtl::ir::core, opcode_range=[0, 0x7FFF]
dialect simd: namespace=dgtl::ir::simd, opcode_range=[0x500, 0x5FF]
dialect gpu: namespace=dgtl::ir::gpu, opcode_range=[0x400, 0x4FF]
dialect tensor: namespace=dgtl::ir::tensor, opcode_range=[0x460, 0x47F]
dialect custom: namespace=dgtl::ir::custom, opcode_range=[0x8000, 0xFFFF]
```

**Dialect Structure:**
- `name`: Dialect identifier
- `namespace`: C++ namespace
- `opcode_range`: [min, max] opcode range
- `description`: Human-readable description

## Section 7: Operation Definitions

```
[operations]
op Add:
  opcode: ADD_I32
  dialect: core
  operands: [lhs: I32, rhs: I32]
  result: I32
  traits: Commutative | HasNoSideEffects
  interfaces: Arithmetic
  description: "32-bit integer addition"
  verifier: "verify_binop"
  builder: "create_binop<OpCode::ADD_I32>"

op Load:
  opcode: LOAD
  dialect: core
  operands: [addr: I64]
  result: I64
  traits: IsMemoryRead
  interfaces: Memory
  memory_access: read
  alignment: 8
  volatile: false

op VecAddF32x16:
  opcode: VEC_ADD_F32_512
  dialect: simd
  operands: [lhs: VectorF32x16, rhs: VectorF32x16]
  result: VectorF32x16
  traits: Commutative | HasNoSideEffects | IsVectorizable
  interfaces: Arithmetic | SIMD
  vector_width: 512
```

**Operation Structure:**
- `name`: Operation identifier (CamelCase)
- `opcode`: C++ OpCode enum value
- `dialect`: Dialect name
- `operands`: List of operands with types
  - Format: `[name: Type, name: Type, ...]`
- `result`: Result type (or `void` if no result)
- `traits`: Pipe-separated trait names
- `interfaces`: Pipe-separated interface names
- `attributes` (optional): Attached attributes
- `memory_access` (optional): read | write | readwrite (for memory ops)
- `alignment` (optional): Memory alignment requirement
- `volatile` (optional): Whether op is volatile
- `vector_width` (optional): SIMD vector width in bits
- `description`: Human-readable description
- `verifier`: Optional custom verification function
- `builder`: Optional custom builder function

## Code Generation Targets

### Generated Header: `DGTL_Gen_Ops.hpp`
```cpp
namespace dgtl::ir::gen {

// Trait enum (bitflags)
enum class GenTrait : uint32_t {
    Commutative = 1u << 0,
    HasNoSideEffects = 1u << 1,
    ...
};

// Interface enum (bitflags)
enum class GenInterface : uint32_t {
    Arithmetic = 1u << 0,
    Memory = 1u << 1,
    ...
};

// Operand descriptor
struct OperandDesc {
    std::string_view name;
    OpCode type_hint;  // For simple cases; full Type system for complex
};

// Result descriptor
struct ResultDesc {
    OpCode type_hint;
};

// Operation metadata
struct OpDef {
    std::string_view name;
    OpCode opcode;
    uint32_t trait_bits;
    uint32_t interface_bits;
    const OperandDesc* operands;  // Array of operands
    uint8_t operand_count;
    ResultDesc result;
    std::string_view description;
    bool has_result;
    bool is_terminator;
    bool is_memory_op;
    bool is_vectorizable;
};

// Fast lookup
const GenOpDef* find_op_def(OpCode opcode) noexcept;

// Builder classes (generated from each op definition)
class AddI32Builder {
public:
    AddI32Builder(Arena& arena) : arena_(arena) {}
    AddI32Builder& with_lhs(VReg lhs) { lhs_ = lhs; return *this; }
    AddI32Builder& with_rhs(VReg rhs) { rhs_ = rhs; return *this; }
    IRNode* build();
private:
    Arena& arena_;
    VReg lhs_, rhs_;
};

// Trait queries
template<OpCode Op>
struct OpTraits {
    static constexpr bool is_commutative = /* traits */ false;
    static constexpr bool has_no_side_effects = false;
    static constexpr bool is_terminator = false;
    static constexpr bool is_memory_read = false;
    static constexpr bool is_memory_write = false;
};

// Interface checks
template<OpCode Op, class Interface>
struct OpImplementsInterface {
    static constexpr bool value = false;
};

} // namespace dgtl::ir::gen
```

## Generator Features

1. **Operation Metadata**: Name, opcode, traits, interfaces, operands, result type
2. **Builder Classes**: Per-operation builder with fluent API
3. **Verifiers**: Automatic signature validation
4. **Trait Lookup**: Fast, compile-time trait queries
5. **Interface Dispatch**: Dynamic interface implementation checks
6. **Type Safety**: Operand type validation in builders
7. **Extensibility**: Support for custom verifiers, builders, attributes

## Usage Example

```cpp
// Using generated builder
auto builder = AddI32Builder(arena)
    .with_lhs(op_a)
    .with_rhs(op_b);
auto add_result = builder.build();

// Using generated metadata
const auto* def = find_op_def(OpCode::ADD_I32);
if (def->trait_bits & static_cast<uint32_t>(GenTrait::Commutative)) {
    // Operands are commutative
}

// Using trait queries
if (OpTraits<OpCode::ADD_I32>::is_commutative) {
    // Reorder operands if beneficial
}

// Using interface checks
if (OpImplementsInterface<OpCode::LOAD, Memory>::value) {
    // Handle memory operation
}
```

## Extension Points

1. **Custom Verifiers**: Link custom C++ functions to operations
2. **Custom Builders**: Provide specialized builder implementations
3. **Attributes**: Attach custom attributes to operations
4. **Dialects**: Define new dialects with custom opcode ranges
5. **Interfaces**: Create new interfaces with custom methods

## Implementation Notes

- Generator is idempotent (running twice produces identical output)
- Generated code is header-only (no symbol dependencies)
- Trait/interface bits are compile-time constants (zero overhead)
- Builders use CRTP for monomorphic inlining
- Type checking happens at operation verification time

