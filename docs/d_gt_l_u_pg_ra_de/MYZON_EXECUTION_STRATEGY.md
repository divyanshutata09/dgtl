# 🎯 MYZON Language: Complete // Execution Strategy

**Author**: Divyanshu Tata / TataG Systems  
**Date**: May 4, 2026  
**Version**: 1.0  
**Scope**: All 88 MYZON Grammar Rules (Complete Coverage)

---

## 📌 Executive Summary

MYZON uses a **three-tier compilation strategy** to achieve maximum performance:

- **** (Ahead-Of-Time): Static structure, safety checks, deterministic layout → 100% compiled before runtime
- **** (Just-In-Time): Hot paths, dynamic behavior, specialization → Compiled at runtime when needed
- ****: Baseline + optimization →  compile once, then  optimize frequently-executed code

This document maps all 88 MYZON grammar rules to their optimal execution strategy with rationale.

---

# 🔷 SECTION I: Root & Top-Level (Rules 1-6)

## Overview
Top-level program structure must be deterministic and verifiable. All imports, platform bindings, and global structure are known at compile time.

### Rule 1: `program` → ****
```myzon
program = { top_stmt } ;
```
**Why ?**
- Program structure is static and known before execution
- Import verification and dependency resolution happens once
- Version checking and module linking is deterministic

**Implementation**:
1. Parse all top-level statements
2. Build symbol table
3. Verify all dependencies
4. Emit baseline binary with relocatable references

**Output**: Complete program blueprint with all imports resolved

---

### Rule 2: `top_stmt` → ****
```myzon
top_stmt = [ doc_string ], { attribute }, ( core_stmt | library_stmt ) ;
```
**Why ?**
- Attributes are metadata for static analysis
- Documentation is pre-processing directive
- Core/library selection is compile-time decision

**Implementation**:
1. Collect all attributes → Store in metadata table
2. Extract documentation → Generate .md or .html
3. Route to appropriate handler (core_stmt or library_stmt)

**Output**: Attributed AST with documentation stripped

---

### Rule 3: `core_stmt` → ****
```myzon
core_stmt = global_binding | import_stmt | use_stmt | from_use_stmt 
          | platform_stmt | routine | entity_def | trait_def 
          | enum_def | partition_stmt | comptime_block ;
```
**Why ?**
- All structural decisions (routines, entities, traits) are static
- Platform bindings determined before runtime
- Global state layout is fixed

**Implementation**:
1. Discriminate statement type
2. Compile each to appropriate IR
3. Build cross-reference table for module linking

**Output**: Fully-linked intermediate representation

---

### Rule 4: `global_binding` → ****
```myzon
global_binding = [ kw_ownership ], ( "let" | "const" ), 
                 identifier, [ ":", type_spec ], 
                 [ "=", expression ], ";" ;
```
**Why ?**
- Declaration is  (layout known)
- Initialization expression may be  (runtime constant folding opportunity)
- Value may be specializable at link time

**Implementation ( Phase)**:
```
1. Allocate static storage location
2. Verify type specification
3. Check ownership constraints
```

**Implementation ( Phase)**:
```
1. Evaluate initialization expression (if not comptime)
2. Apply constant folding if possible
3. Specialize based on usage patterns
```

**Output**: Global symbol with address + cached initialization value

---

### Rules 5–5b: Import/Use/From-Use Statements → ****
```myzon
import_stmt = "import", ( string | identifier ), 
              [ "as", identifier ], [ "version", string ], 
              "verify", hex_addr, ";" ;

use_stmt = "use", ( string | identifier ), 
           [ "as", identifier ], "verify", hex_addr, ";" ;

from_use_stmt = "from", ( string | identifier ), 
                "use", "<", identifiers, ">", 
                "verify", hex_addr, ";" ;
```
**Why ?**
- Module resolution is deterministic
- Version verification happens before execution
- Hash verification (`hex_addr`) is compile-time check

**Implementation**:
```
1. Resolve module path (import/use/from-use)
2. Verify version constraint (if specified)
3. Check content hash against declared value
4. Load symbol table from module
5. Create qualified name bindings (as alias if specified)
```

**Output**: Loaded module symbol table + relocation entries

---

### Rule 6: `platform_stmt` → ****
```myzon
platform_stmt = "platform", identifier, "{", 
                { identifier, ":", expression, ";" }, 
                [ "check", expression, ";" ], "}" ;
```
**Why ?**
- Platform capabilities are known at compile time
- Feature detection can happen during  compilation
- Target architecture is fixed

**Implementation**:
```
1. Extract platform identifier (e.g., "x86_64", "arm64")
2. Compile platform-specific expressions (e.g., "has_avx512")
3. Evaluate check expressions:
   - If false: Error (platform incompatible)
   - If true: Proceed with platform-specific code paths
4. Emit platform-specific binary selection
```

**Output**: Platform-gated code with compile-time feature bits

---

# 🔷 SECTION II: Lexical Core (Rules 7–15)

## Overview
All lexical elements are compile-time constructs. No runtime interpretation needed.

### Rules 7–10: Digit & Number Components → ****
**Why ?** Lexical elements are deterministic patterns. No runtime evaluation.

**Implementation**:
```
- Tokenize based on pattern matching
- Store as constants or immediates
- No dynamic interpretation
```

---

### Rule 11–11e: Number Literals → ****
```myzon
bin_literal  = "0b", bin_digit, { bin_digit | "_" }, [ number_suffix ] ;
oct_literal  = "0o", oct_digit, { oct_digit | "_" }, [ number_suffix ] ;
hex_literal  = "0x", hex_digit, { hex_digit | "_" }, [ number_suffix ] ;
dec_literal  = digit, { digit | "_" }, [ ".", ... ], [ exp_part ], [ number_suffix ] ;
hex_float    = "0x", hex_digit, { hex_digit | "_" }, [ ".", ... ], hex_exp_part, [ number_suffix ] ;
```
**Why ?**
- Literals are constants
- Type inference is static
- No runtime conversion needed

**Implementation**:
```
1. Parse base-specific format (binary, octal, hex, decimal)
2. Apply number suffix (u64, f32, etc.)
3. Create constant IR node with typed value
4. Underscores are ignored (for readability)
```

**Output**: Typed constant value

---

### Rules 12–13d: Strings & Characters → ****
```myzon
escape_seq = "\\" , ( '"' | "'" | '\\' | "n" | "r" | "t" | "0" 
           | ( "u", "{", hex_digit, { hex_digit }, "}" ) ) ;

char_literal = "'", char_char, "'" ;
string = '"', { string_char }, '"' ;
raw_string = "r", { "#" }, '"', { %x00-10FFFF }, '"', { "#" } ;
```
**Why ?**
- String encoding (UTF-8) is static
- Escape sequences are compile-time interpreted
- Raw strings avoid interpretation overhead

**Implementation**:
```
1. For strings/chars:
   - Decode escape sequences → UTF-8 bytes
   - Validate Unicode validity
   - Intern string (deduplicate identical strings)
   - Store in read-only data section

2. For raw strings:
   - Count leading/trailing '#' symbols
   - Verify matching count on both sides
   - Store content as-is (no escape processing)
```

**Output**: UTF-8 encoded string constant in .rodata section

---

### Rules 14–14c: Comments → **** (stripped during lexing)
**Why ?**
- Comments are metadata
- Stripped immediately during lexical analysis
- Not part of IR

**Implementation**:
```
1. Standard comments (//) → Skip to newline
2. Doc comments (///) → Extract and store in metadata
3. Block comments (/* */) → Skip content, track nesting
```

**Output**: Metadata table with documentation strings

---

### Rule 15: Literals → ****
```myzon
literal = number | string | raw_string | char_literal 
        | "true" | "false" | "null" ;
```
**Why ?** All literal types are constants → Compile-time evaluation.

---

# 🔷 SECTION III: Reserved Vocabulary (Rules 16–17)

### Rule 16: `kw_ownership` → ****
```myzon
kw_ownership = "owned" | "shared" | "&" | "const" | "atomic" 
             | "isolated" | "mut" | "auto" | "volatile" | "linear" 
             | "managed" | "opaque" | "sealed" | "cap" ;
```
**Why ?**
- Ownership is a compile-time memory model
- Determines layout, threading model, and optimizations
- Checked during type checking

**Implementation**:
```
1. Parse ownership keyword
2. Attach to binding/parameter/field declaration
3. Enforce ownership rules during type checking:
   - "owned": Unique reference (no aliasing)
   - "shared": Multiple immutable references
   - "&": Borrowed reference (temporary)
   - "const": Immutable binding
   - "atomic": Atomic access (built-in synchronization)
   - "linear": Exactly-once consumption (affine type)
   - "volatile": Prevents optimization
   - Other keywords: Per-implementation strategy
```

**Output**: Ownership annotation in IR

---

### Rule 17: `kw_concur` → ****
```myzon
kw_concur = "spawn" | "async" | "fiber" | "elastic" | "thread" 
          | "sync" | "await" | "join" | "lock" | "release" 
          | "fence" | "recv" | "send" | "barrier" ;
```
**Why ?**
- : Structure and synchronization points are known
- : Actual threading/scheduling happens at runtime
- Runtime: Workload patterns determine thread pool sizing

**Implementation ( Phase)**:
```
1. Parse concurrency keyword
2. Validate usage context (e.g., spawn only in fn body)
3. Inject synchronization hooks in IR
4. Emit baseline code with runtime hooks
```

**Implementation ( Phase)**:
```
1. Monitor actual thread contention
2. Adjust thread pool size
3. Specialize synchronization (e.g., spinlock vs. futex)
4. Profile memory ordering (fence optimization)
```

**Output**:  IR +  tuning metadata

---

# 🔷 SECTION IV: Type Architecture (Rules 18–22)

## Overview
Types are static, but type specialization can happen at runtime (generics, ADT matching).

### Rule 18: `primitive` → ****
```myzon
primitive = "f64" | "f32" | "f16" | "bf16" | "f8" 
          | "i64" | "i32" | "i4" | "u64" | "u8" | "u4" 
          | "bit" | "bool" | "complex64" | "void" | "any" 
          | "endpoint" | "auto" | "self" | "string" | "char" ;
```
**Why ?**
- Primitive types have fixed sizes and representations
- Type checking is compile-time
- ABI calling conventions determined by types

**Implementation**:
```
1. Map primitive name to internal type representation
2. Store size, alignment, calling convention
3. For "auto" and "any": Defer to type inference/runtime checks
4. For "self": Resolve to enclosing type
```

**Output**: Type metadata table

---

### Rule 19: `dsa_type` (Data Structure Abstraction) → ****
```myzon
dsa_type = "tensor" | "graph" | "tree" | "trie" | "node" | "edge" ;
```
**Why ?**
- DSA layout depends on usage patterns
- Specialization based on actual data
- Optimization opportunities at runtime

**Implementation ( Phase)**:
```
1. Define generic DSA interface
2. Emit specialization hooks
```

**Implementation ( Phase)**:
```
1. Observe actual usage (size, access pattern, mutation)
2. Specialize layout (e.g., compressed tensor vs. dense)
3. Optimize for cache locality
```

**Output**: Specialized DSA implementation selected at runtime

---

### Rule 20: `collection_type` → ****
```myzon
collection_type = "array" | "list" | "map" | "set" | "chan" 
                | "stream" | "queue" | "stack" | "heap" | "deque" ;
```
**Why ?**
- : Collection interface is static
- : Backing store can be specialized (array vs. linked list, hash vs. tree)
- Runtime: Capacity hints allow optimal initial allocation

**Implementation**:
```
:
  1. Define collection interface (push, pop, get, set)
  2. Emit vtable for polymorphic dispatch

:
  1. Monitor access patterns
  2. Specialize to array if sequential, map if random access
  3. Tune capacity growth factor based on pressure
```

**Output**: Polymorphic collection with optimized backing store

---

### Rule 21: `type_spec` → ****
```myzon
type_spec = ( primitive | dsa_type | collection_type | identifier ), 
            [ "<", type_spec, { ",", type_spec }, ">" ], 
            { "[", [ expression ], "]" }, [ "?" ] ;
```
**Why ?**
- Base type checking is static
- Generic specialization happens at compile time or link time
- Array dimensions are static (or constrained)

**Implementation**:
```
1. Parse base type (primitive/collection/user-defined)
2. Extract generic parameters: type_spec recursively
3. Parse array bounds:
   - If empty []: Dynamic array
   - If expression: Array of size (may need  evaluation if non-constant)
4. Handle optional "?" (nullable type)
5. Resolve user-defined identifier against symbol table
```

**Output**: Fully-qualified type with generics resolved

---

### Rule 22: `attribute` → ****
```myzon
attribute = "@", identifier, [ "(", [ map_entries ], ")" ] ;
```
**Why ?**
- Attributes are metadata directives
- Processed during compilation
- Control code generation, inlining, etc.

**Implementation**:
```
1. Parse @identifier and optional parameters
2. Common attributes:
   - @inline: Force function inlining
   - @noinline: Prevent inlining
   - @hot: Mark hot function ( optimization target)
   - @cold: Mark cold function (reduce code size)
   - @target("avx512"): Require specific ISA
   - @check: Assertion/verification (comptime)
3. Store in annotation table
4. Emit corresponding IR directives
```

**Output**: Attribute list attached to declaration

---

# 🔷 SECTION V: Expression Engine (Rules 23–34)

## Overview
Expressions are **** with specialized cases.

### Rule 23: `expression` → ****
```myzon
expression = pipeline ;
```
**Why ?**
- Simple expressions compile at 
- Complex expressions may be -optimized
- Runtime evaluation when needed

**Implementation**:
```
1. Parse as pipeline (delegate to rule 24)
2. Apply constant folding at  if fully resolvable
3. : Profile and optimize hot expressions
```

---

### Rule 24: `pipeline` → ****
```myzon
pipeline = logic_or, { "|>", logic_or } ;
```
**Why ?**
- Pipelines represent dataflow chains
- Fusion opportunity (combine multiple operations)
- Specialization based on actual data types

**Implementation ( Phase)**:
```
1. Parse pipe operators
2. Generate straightforward code
3. Insert fusion barriers (markers for optimization)
```

**Implementation ( Phase)**:
```
1. Detect pipeline patterns
2. Fuse adjacent stages into single kernel
3. Eliminate intermediate allocations
4. Vectorize if applicable
```

**Example**:
```myzon
data |> filter(x => x > 5) |> map(x => x * 2) |> sum
```
** Fusion**: Combine filter+map+sum into single loop → ~3x speedup

---

### Rules 25–33: Arithmetic & Logical Operators → ****
```myzon
logic_or       = logic_and, { "||", logic_and } ;
logic_and      = equality, { "&&", equality } ;
equality       = relational, { ("==" | "!=" | "is" | "in" | "as" | "matches"), relational } ;
relational     = additive, { (">=" | "<=" | ">" | "<"), additive } ;
additive       = multiplicative, { ("+" | "-"), multiplicative } ;
multiplicative = power, { ("*" | "/" | "%" | "@" | "??"), power } ;
power          = unary, [ "**", power ] ;
unary          = { "!" | "~" | "-" | "++" | "--" | "new" | "grad" | "typeof" 
                 | "&" | "*" | "comptime" }, postfix ;
postfix        = primary, { ("." (identifier | number)) 
                          | ("(" [ args ] ")")
                          | ("[" expression "]") 
                          | "?" | "!" } ;
```

**Why ?**
- : Generate correct baseline code
- : Specialize for actual operand types
- Optimization: Combine operators in hot loops

**Implementation**:
```
:
  1. Parse operator precedence correctly
  2. Generate straightforward IR:
     - logic_or/and → conditional branches or bitwise
     - equality/relational → comparison instructions
     - additive/multiplicative → arithmetic IR
     - power (**) → libm call or SIMD fma/exponential
     - unary operators → single-operand instructions
     - postfix → member access, call, indexing

:
  1. Profile operator combinations
  2. Specialize for types: int fast path vs. float vs. pointer
  3. SIMD vectorization for arithmetic chains
  4. Branch prediction tuning for logic operators
```

**Output**: Type-specialized expression code

---

### Rule 34: `primary` → ****
```myzon
primary = literal | ("(" expression ")") | query_lib | lambda 
        | collection_lit | dsa_lit | identifier ;
```
**Why ?**
- Literals →  (compile-time constants)
- Parenthesized expressions →  (recursion)
- Query/lambda/DSA/collections →  (dynamic behavior)
- Identifier →  (symbol lookup)

**Implementation**:
```
:
  1. Literals: Inline as constants
  2. Identifiers: Resolve from symbol table
  3. Parentheses: Recurse to expression handler

:
  1. Query: Build and optimize query plan
  2. Lambda: -compile captures and body
  3. Collections/DSA: Specialize based on usage
```

---

# 🔷 SECTION VI: Data Structure Literals (Rules 35–41)

### Rule 35: `lambda` → ****
```myzon
lambda = ( ("|" [ params ] "|") | identifier ), "=>", 
         (expression | block) ;
```
**Why ?**
- Closures capture runtime values
- Body execution depends on actual arguments
- Specialization based on call sites

**Implementation ( Phase)**:
```
1. Parse lambda signature
2. Identify captured variables
3. Generate closure wrapper function
4. Emit thunk for closure creation
```

**Implementation ( Phase)**:
```
1. Analyze call sites and argument types
2. Specialize lambda for each context
3. Inline lambda into call site if beneficial
4. Eliminate captured variable indirection if constant
```

**Output**: Specialized lambda instances at hot call sites

---

### Rule 36: `collection_lit` → ****
```myzon
collection_lit = ("[" [ args ] "]") | ("{" [ map_entries] "}")  ;
```
**Why ?**
- : Structure is known
- : Specialization based on element types and sizes

**Implementation**:
```
:
  1. Allocate collection storage
  2. Initialize from literal values

:
  1. If all elements same type: Use monomorphic storage
  2. If mixed types: Use polymorphic storage (slower)
  3. Optimize for size: Small buffer optimization vs. heap
```

---

### Rule 37: `dsa_lit` → ****
```myzon
dsa_lit = ( ("graph" | "tree" | "trie"), "{", { edge_definition }, "}" )
        | ( ("tensor" | "array" | "list"), "[", [ args ], "]" ) ;
```
**Why ?**
- DSA construction depends on data
- Specialization for actual usage pattern
- Runtime layout optimization

**Implementation ( Phase)**:
```
1. Parse DSA type
2. Generate generic constructor
```

**Implementation ( Phase)**:
```
1. Monitor DSA operations (insert, search, traverse)
2. Adapt layout: compressed, sparse, dense based on patterns
3. Optimize for cache locality
```

---

### Rules 38–41: Collections/Maps/Args → ****
```myzon
edge_definition = expression, ("->" | "<->" | "=>"), expression, 
                  [ ":", expression ], ";" ;
map_entries = map_entry, { ",", map_entry } ;
map_entry = (literal | identifier), ":", expression ;
args = expression, { ",", expression } ;
```
**Why ?**
- Structure known (static)
- Evaluation of expressions is dynamic

**Implementation**:
```
:
  1. Parse structure
  2. Generate code to evaluate each expression

:
  1. Cache results if expressions are pure
  2. Parallelize independent expressions if beneficial
```

---

# 🔷 SECTION VII: Procedural Execution (Rules 42–59)

## Overview
Statements are **** with control flow being specializable.

### Rules 42–44: Scope & Block & Statement → ****
```myzon
scope = { statement, [ ";" ] } ;
block = "{", scope, "}" ;
statement = binding | flow | loop | concurrent_op | yield_stmt 
          | try_block | sys_op | asm_block | shell_stmt 
          | assignment_stmt | expr_stmt | verify_block ;
```

**Why ?**
- : Baseline execution correct
- : Optimize statement sequences in hot blocks

**Implementation**:
```
:
  1. Generate code for each statement sequentially
  2. No dead code elimination yet

:
  1. Remove unreachable code
  2. Combine adjacent statements if beneficial
  3. Specialize control flow (inline branches)
```

---

### Rules 45–48: Assignment → ****
```myzon
assignment_stmt = l_value, assign_op, expression ;
expr_stmt = expression ;
binding = [ kw_ownership ], ("let" | "mut" | "const" | "var"), 
          l_value, [ ":", type_spec ], [ "=", expression ] ;
l_value = (identifier | ("[" identifiers "]") 
          | ("{" identifiers "}")), 
          { ("." (identifier | number)) | "[" expression "]" } ;
```

**Why ?**
- : Correct semantics (lifetime, ownership)
- : Eliminate bounds checking, optimize memory access

**Implementation**:
```
:
  1. Resolve l_value to storage location
  2. Type-check r_value expression
  3. Emit basic store instruction

:
  1. For repeated indexed access: Bound check elimination
  2. For local variables: Stack-promote escaping analysis
  3. For assignments in loops: Vectorization if applicable
```

---

### Rule 49: `assign_op` → ****
```myzon
assign_op = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" 
          | "^=" | "<<=" | ">>=" | "@=" | "|>=" | "??=" ;
```
**Why ?**
- Operator semantics are fixed
- Desugaring happens at compile time

**Implementation**:
```
1. Desugar compound assignments:
   x += y  →  x = x + y
   x |>= f →  x = x |> f
2. Generate appropriate instruction
```

---

### Rules 50–55: Control Flow → ****
```myzon
flow = if_stmt | switch_stmt | match_stmt | select_stmt ;
if_stmt = "if", expression, [ (":high" | ":low") ], block, 
          { "elif", expression, block }, 
          [ "else", block ] ;
switch_stmt = "switch", expression, "{", 
              { "case", args, ":", scope }, 
              [ "default", ":", scope ], "}" ;
match_stmt = "match", expression, "{", { match_arm }, "}" ;
match_arm = (literal | collection_lit | identifier | "_"), 
            [ "if", expression ], ":", (expression | block) ;
select_stmt = "select", "{", { concurrent_op, ":", scope }, "}" ;
```

**Why ?**
- : Generate correct branches
- : Specialize based on branch frequency

**Implementation ( Phase)**:
```
1. if/elif/else:
   - Generate comparison
   - Emit conditional branch
   - Branch hints (:high/:low) → likely/unlikely

2. switch/match:
   - Build jump table if contiguous cases
   - Use binary search for sparse cases
   - Fallthrough handling

3. select (concurrency):
   - Generate ready polling loop
   - Emit communication primitives
```

**Implementation ( Phase)**:
```
1. Profile actual branch frequencies
2. Reorder branches: hot path first
3. Inline cold branches
4. For match: Specialize pattern matching based on data
```

---

### Rules 56–59: Loops → ****
```myzon
loop = for_loop | while_loop | do_while ;
for_loop = "for", ( ("(" [ binding ] ";" [ expression ] ";" 
                         [ expression ] ")")
                  | ((identifier | "[" identifier "," identifier "]"), 
                     "in", expression) ), block ;
while_loop = "while", expression, block ;
do_while = "do", block, [ "while", expression ] ;
```

**Why ?**
- : Generate correct loop structure
- : Extensive optimization opportunities (vectorization, unrolling)

**Implementation ( Phase)**:
```
1. for-loop (C-style):
   - binding: Initialize loop variable
   - condition: Emit loop test
   - increment: Emit update expression
   - Emit branch-back

2. for-loop (iterator):
   - Call iterator method on expression
   - Emit loop body for each iteration

3. while/do-while: Standard loop code generation
```

**Implementation ( Phase)**:
```
1. Loop unrolling: Replicate loop body N times
2. Vectorization: Pack multiple iterations into SIMD operations
3. Strength reduction: x*2 → x << 1
4. Invariant hoisting: Move constant expressions out of loop
5. Bounds check elimination: Remove redundant checks
6. Prefetching: Emit CPU cache prefetch instructions
7. Parallelization: Emit thread spawning if iterations independent
```

**Output**: Optimized loop with  specialization

---

# 🔷 SECTION VIII: System Hooks (Rules 60–65)

### Rule 60: `concurrent_op` → ****
```myzon
concurrent_op = kw_concur, identifier, [ "<", type_spec, ">" ], 
                "(", [ args ] ")", [ "timeout", number ] ;
```
**Why ?**
- Threading/async decisions depend on workload
- Timeout values may be tuned at runtime
- Thread pool sizing is dynamic

**Implementation ( Phase)**:
```
1. Parse concurrency keyword and function
2. Generate baseline threading code
3. Insert runtime hooks for thread creation
```

**Implementation ( Phase)**:
```
1. Monitor task scheduling
2. Tune thread pool size based on actual parallelism
3. Specialize spawning strategy (thread vs. fiber vs. tasklet)
4. Apply timeout tuning if threshold exceeded
```

---

### Rule 61: `yield_stmt` → ****
```myzon
yield_stmt = "yield" | ("return", [ expression ]) 
           | "break" | "continue" 
           | ("panic", [ expression ]) ;
```

**Why ?**
- : Correct control flow (return, break, etc.)
- : Optimize return values, panic handling

**Implementation**:
```
:
  1. yield: In generators, emit yield marker
  2. return: Emit stack unwinding + return instruction
  3. break/continue: Emit branch to loop exit/top
  4. panic: Emit exception/handler dispatch

:
  1. Specialize return based on actual value types
  2. Optimize panic paths (reduce instrumentation on hot paths)
  3. Elide exception handling if provably unnecessary
```

---

### Rule 62: `try_block` → ****
```myzon
try_block = "try", block, { "catch", [ identifier, ":", type_spec ], 
            block }, [ "retry", number ], 
            [ "finally", block ] ;
```

**Why ?**
- : Emit exception unwinding tables
- : Optimize catching (rare case optimization)

**Implementation ( Phase)**:
```
1. Generate try block code
2. Build exception dispatch table (which catch handles which type)
3. Generate catch handlers
4. Emit finally block execution (both normal + exception path)
5. Emit retry logic with counter
```

**Implementation ( Phase)**:
```
1. If exceptions are rare: Optimize normal path, penalize exception path
2. If exceptions common: Optimize exception dispatch
3. Specialize catch handlers based on actual exception types seen
```

---

### Rule 63: `sys_op` → ****
```myzon
sys_op = ("print" | "println"), "(", [ args ] ")" ;
```

**Why ?**
- Print is I/O, which benefits from buffering optimization
- Format string specialization at runtime

**Implementation ( Phase)**:
```
1. Parse print/println (println = unbuffered)
2. Generate code to evaluate arguments
3. Emit print syscall/function
```

**Implementation ( Phase)**:
```
1. For println: Unbuffered I/O
2. For print: Buffer outputs, flush periodically
3. Specialize for common format strings
4. Vectorize if printing arrays
```

---

### Rule 64: `shell_stmt` → ****
```myzon
shell_stmt = "$", (string | identifier | ("{" expression "}")) ;
```

**Why ?**
- Shell execution is dynamic
- Command may depend on runtime values
- Process spawning is expensive → should be optimized

**Implementation ( Phase)**:
```
1. Parse shell statement
2. Generate process spawn code
```

**Implementation ( Phase)**:
```
1. Cache command results if deterministic
2. Parallelize independent shell commands
3. Use shell builtins instead of forking if possible
```

---

### Rule 65: `asm_block` → ****
```myzon
asm_block = "asm", "isolated", identifier, "{", 
            { %x00-7A | %x7C | %x7E-10FFFF }, "}" ;
```

**Why ?**
- Inline assembly is platform-specific
- Optimization depends on CPU features
- Isolation requirement enforced at runtime

**Implementation**:
```
:
  1. Parse inline assembly (identifier = dialect, e.g., "x86_64")
  2. Perform basic validation
  3. Mark as "isolated" (no optimizer can touch this block)

:
  1. At link time: Specialize for target CPU
  2. Apply CPU-specific optimizations (e.g., AVX-512 vs. AVX2)
  3. Verify isolation constraints
```

---

# 🔷 SECTION IX: Structural Core (Rules 66–74)

### Rule 66: `routine` → ****
```myzon
routine = { attribute }, "fn", [ "width", "<", number, ">" ], 
          identifier, [ "<", identifiers, ">" ], 
          "(", [ params ] ")", [ ":", type_spec ], 
          [ "check", expression ], [ verify_block ], 
          (block | ";") ;
```

**Why ?**
- : Function signature, type checking, ABI binding
- : Function specialization, inlining, optimizations

**Implementation ( Phase)**:
```
1. Parse function signature
2. Type-check parameters and return type
3. Resolve generic parameters (if any)
4. Build symbol table for function scope
5. Compile function body (recursively apply rules 42–65)
6. Generate function prologue/epilogue (stack frame setup)
```

**Implementation ( Phase)**:
```
1. Profile actual call sites
2. Specialize generics for observed types
3. Inline small hot functions
4. Apply loop optimizations
5. SIMD vectorization opportunities
```

**Special Case: `width<N>`**
- SIMD vector width hint: Process N elements in parallel
- : Specialize for actual SIMD capabilities

**Output**: Callable function with  specialization hooks

---

### Rules 67–68: Parameters → ****
```myzon
params = (param, { ",", param }, [ ",", "...", identifier, ":", type_spec ])
       | ("...", identifier, ":", type_spec) ;
param = [ kw_ownership ], identifier, [ ":", type_spec ], 
        [ "=", expression ] ;
```

**Why ?**
- Parameter passing convention is compile-time
- Type checking is static

**Implementation**:
```
1. Parse each parameter
2. Assign calling convention based on type and ownership
3. For default values: Evaluate at compile-time if constant
4. For variadic (...): Generate array packing code
```

---

### Rule 69: `entity_def` → ****
```myzon
entity_def = ("struct" | "class" | "actor"), identifier, 
             [ "<", identifiers, ">" ], [ ":", identifier ], 
             "{", { field_decl | routine | ("check", expression, ";") 
                  | verify_block }, "}" ;
```

**Why ?**
- Struct layout is compile-time (alignment, offset)
- Class hierarchy is static
- Actor behavior is deterministic

**Implementation**:
```
1. Parse entity type (struct/class/actor)
2. Extract base class (if any)
3. For each field_decl: Calculate offset, alignment
4. Build vtable for methods (class/actor)
5. Compile check expressions and verify blocks
6. Emit metadata (RTTI for classes)
```

**Output**: Type definition with vtable (if needed)

---

### Rule 70: `field_decl` → ****
```myzon
field_decl = [ kw_ownership ], identifier, [ ":", type_spec ], 
             [ "align", number ], [ "at", hex_addr ], 
             [ "=", expression ], ";" ;
```

**Why ?**
- Memory layout is compile-time
- Alignment and address calculation is deterministic

**Implementation**:
```
1. Extract field name and type
2. Ownership: Determines reference semantics
3. align: Override default alignment
4. at: Place field at specific offset (FFI/hardware register mapping)
5. = default value: Store in class metadata
```

---

### Rules 71–73: Trait/Enum Definitions → ****
```myzon
trait_def = "trait", identifier, [ "<", identifiers, ">" ], 
            "{", { routine | ("check", expression, ";") 
                 | verify_block }, "}" ;
enum_def = "enum", identifier, "{", 
           [ enum_val, { ",", enum_val } ], "}" ;
enum_val = identifier, [ "(", type_spec, ")" ], [ "=", number ] ;
```

**Why ?**
- Traits define compile-time interfaces
- Enum layout is static

**Implementation**:
```
trait:
  1. Build interface with method signatures
  2. For each routine: Emit virtual method descriptor

enum:
  1. Assign discriminant values (0, 1, 2, ... or explicit)
  2. For each variant with data: Store layout info
  3. Emit enum matching helpers (tag extraction, pattern matching)
```

---

### Rule 74: `partition_stmt` → ****
```myzon
partition_stmt = "partition", "sealed", identifier, 
                 [ "elastic" ], "|", number, 
                 ( "kb" | "mb" | "gb" | "unlimited" ), "|", block ;
```

**Why ?**
- Memory partition sizing is runtime decision
- "elastic" indicates dynamic adjustment
- Actual memory pressure determines partition size

**Implementation ( Phase)**:
```
1. Parse partition name and size hint
2. Parse "elastic" flag
3. Compile block code
```

**Implementation ( Phase)**:
```
1. Allocate initial partition at declared size
2. Monitor pressure (if elastic):
   - Exceed threshold: Expand partition
   - Underutilized: Shrink partition
3. Profile memory access patterns
4. Optimize allocation strategy
```

---

# 🔷 SECTION X: Meta-Programming & Proof (Rules 75–76)

### Rule 75: `comptime_block` → ****
```myzon
comptime_block = "comptime", block ;
```

**Why ?**
- : Execute at compile time
- : Specialize comptime results based on runtime values

**Implementation ( Phase)**:
```
1. Execute block at compile time (using comptime interpreter)
2. Capture results (constants, generated code, metadata)
3. Embed results in compiled binary
```

**Implementation ( Phase)**:
```
1. If comptime depends on generic type parameters:
   2. Re-execute comptime for each specialization
```

---

### Rule 76: `verify_block` → ****
```myzon
verify_block = "verify", "{", { expression, [ ";" ] }, "}" ;
```

**Why ?**
- Verification is a compile-time assertion
- Enables exhaustive proof checking

**Implementation**:
```
1. Parse verification expressions
2. Attempt to prove each expression true:
   - If provable: Continue
   - If false: Compilation error
   - If unprovable: Warning (may enable at runtime)
3. Emit runtime assertions for unprovable cases (if requested)
```

---

# 🔷 SECTION XI: Libraries & Enclaves (Rules 77–88)

### Rule 77: `library_stmt` → ****
```myzon
library_stmt = model_lib | service_lib | component_lib 
             | layout_lib | foreign_bind ;
```

**Why ?**
- Libraries contain DSLs requiring runtime interpretation
- Specialization based on data/configuration

**Routing to sub-rules 79–87**

---

### Rule 78: `query_lib` → ****
```myzon
query_lib = "from", (identifier | ("(" expression ")")), 
            { ("where" | "order" | "group" | "limit" | "join"), 
              expression }, 
            [ "select", args ] ;
```

**Why ?**
- Query execution requires runtime optimization
- Query planning is dynamic

**Implementation ( Phase)**:
```
1. Parse query structure (from → where → group → order → select)
2. Generate AST for query plan
```

**Implementation ( Phase)**:
```
1. Analyze actual data statistics
2. Build optimized query execution plan:
   - Push predicates (where clauses) early
   - Optimize join order
   - Use indexes if available
3. Emit specialized query code
```

---

### Rule 79: `model_lib` → ****
```myzon
model_lib = "model", "isolated", identifier, "{", 
            { ("layer", identifier, ":", identifier, 
               "(", [ args ] ")") | routine }, "}" ;
```

**Why ?**
- ML models require runtime specialization
- Layer execution order may be optimized
- Forward pass specializes based on input shapes

**Implementation ( Phase)**:
```
1. Parse model layers
2. Build computation graph
3. Generate baseline forward pass
```

**Implementation ( Phase)**:
```
1. Observe input tensor shapes
2. Specialize layer kernels for actual shapes
3. Fuse adjacent layers into single kernel
4. Apply backend-specific optimizations (cuDNN, TensorRT)
```

---

### Rule 80: `service_lib` → ****
```myzon
service_lib = "service", identifier, "{", 
              { gateway_lib | routine | policy_lib }, "}" ;
```

**Why ?**
- Services are runtime entities
- Scheduling, routing, load balancing are dynamic

**Implementation ( Phase)**:
```
1. Parse service definition
2. Generate baseline RPC dispatcher
3. Build routing table skeleton
```

**Implementation ( Phase)**:
```
1. Monitor request patterns
2. Specialize routing for hot paths
3. Optimize RPC marshalling for common types
4. Apply rate limiting based on actual load
```

---

### Rule 81: `gateway_lib` → ****
```myzon
gateway_lib = "gateway", identifier, "on", (number | string), 
              "secure", "{", { routine }, "}" ;
```

**Why ?**
- Gateway ports, security levels are runtime
- Connection handling is dynamic

**Implementation**:
```
:
  1. Parse gateway definition
  2. Generate secure transport setup

:
  1. Bind to port (may be dynamic, e.g., 0 = any available)
  2. Establish TLS/DTLS if "secure"
  3. Accept connections and dispatch to routines
```

---

### Rule 82: `policy_lib` → ****
```myzon
policy_lib = "policy", identifier, "{", 
             [ identifier, ":", expression, 
               { ",", identifier, ":", expression } ], "}" ;
```

**Why ?**
- : Policy structure is static
- : Policy evaluation depends on runtime conditions

**Implementation**:
```
:
  1. Parse policy key-value pairs
  2. Type-check expressions

:
  1. Evaluate policy expressions at runtime
  2. Cache results if deterministic
  3. Adapt based on actual system state
```

---

### Rule 83: `component_lib` → ****
```myzon
component_lib = "component", identifier, "{", 
                [ ui_element, { ",", ui_element } ], "}" ;
```

**Why ?**
- UI component rendering is runtime
- Specialization based on display size, capabilities

**Implementation ( Phase)**:
```
1. Parse component definition
2. Build component tree
3. Generate render function
```

**Implementation ( Phase)**:
```
1. At runtime: Render components for actual display
2. Specialize for screen size, DPI
3. Cache rendered output
4. Optimize redraw areas
```

---

### Rules 84–85: UI Elements → ****
```myzon
ui_element = identifier, [ "(", [ map_entries ] ")" ], 
             [ "[", [ ui_child_item, { ",", ui_child_item } ], "]" ] ;
ui_child_item = ui_element | ("{" expression "}") ;
```

**Why ?**
- UI rendering is runtime-dependent
- Specialization based on actual display and data

---

### Rule 86: `layout_lib` → ****
```myzon
layout_lib = "layout", identifier, [ "<", identifier, ">" ], 
             "|", { field_decl }, "|", "{", 
             [ "map", "{", [ "tensor", identifier, "<", args, ">", 
               ":", primitive, [ "align", number ], 
               { ",", "tensor", identifier, "<", args, ">", 
               ":", primitive, [ "align", number ] } ], "}" ], 
             [ "kernel", "{", { routine }, "}" ], "}" ;
```

**Why ?**
- Memory layout optimization is data-dependent
- Specialization for tensor shapes and dtypes

**Implementation ( Phase)**:
```
1. Parse layout fields and tensor mappings
2. Generate default memory layout
```

**Implementation ( Phase)**:
```
1. Observe actual tensor usage patterns
2. Specialize layout for observed shapes
3. Optimize memory access (cache locality)
4. Compile kernels with shape-specific parameters
```

---

### Rule 87: `foreign_bind` → ****
```myzon
foreign_bind = "foreign", identifier, "maps", identifier, string, 
               "verify", hex_addr, 
               [ "symbol", sym_op, "maps", identifier ], 
               "endbind" ;
```

**Why ?**
- : Load FFI module, verify hash
- : Specialize wrapper for calling convention

**Implementation ( Phase)**:
```
1. Parse foreign library binding
2. Load library (identifier = library name)
3. Verify content hash (hex_addr)
4. Extract function symbol
5. Generate FFI wrapper function
```

**Implementation ( Phase)**:
```
1. Profile FFI call patterns
2. Specialize wrapper for actual call types
3. Inline frequent calls
4. Reduce marshalling overhead
```

---

### Rule 88: `sym_op` → ****
```myzon
sym_op = "!" | "#" | "$" | "%" | "^" | "&" | "*" | "+" | "-" 
       | "=" | "/" | ">" | "<" | "|" | "~" | "@" ;
```

**Why ?**
- Symbol mapping is deterministic
- Used for C/C++ name mangling

**Implementation**:
```
1. Map MYZON symbol operation to C++ mangling suffix
2. Used in foreign_bind for symbol name transformation
```

---

# 📊 Complete Summary Table

## All 88 Rules: Execution Strategy

| Range | Category | Count | Strategy | Rationale |
|-------|----------|-------|----------|-----------|
| 1–6 | Root & Top-Level | 6 | **** | Static program structure |
| 7–15 | Lexical Core | 9 | **** | Compile-time constants |
| 16–17 | Keywords | 2 | **/** | Ownership (), Concurrency () |
| 18–22 | Types | 5 | **//** | Base (), DSA (), Collections () |
| 23–34 | Expressions | 12 | **** | Baseline +  optimization |
| 35–41 | Data Structures | 7 | **/** | Dynamic specialization |
| 42–65 | Procedural | 24 | **** | Baseline + Hot-path optimization |
| 66–76 | Structural | 11 | **/** | Functions (), Types () |
| 77–88 | Libraries/DSLs | 12 | **/** | Runtime interpretation + specialization |
| **TOTAL** | | **88** | | |

---

## Execution Timeline

```
┌─────────────────────────────────────────────────────────┐
│ COMPILATION PHASE ()                                 │
│ ├─ Parse + Type Check (Rules 1, 3, 7–22, 49, 67–73)  │
│ ├─ Symbol Resolution (Rules 5–6)                       │
│ ├─ Code Generation (Rules 42–59, 66, 69–71)           │
│ └─ Emit Baseline Binary +  Hooks                     │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ RUNTIME PHASE (Baseline Execution)                      │
│ ├─ Execute  code paths                              │
│ ├─ Collect profiling data                              │
│ └─ Identify hot spots                                  │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ OPTIMIZATION PHASE ()                                │
│ ├─ Specialize hot functions (Rule 66)                  │
│ ├─ Vectorize loops (Rules 56–59)                       │
│ ├─ Inline functions (Rule 66)                          │
│ ├─ Optimize query/DSA (Rules 78–79, 86)               │
│ ├─ Tune collection strategies (Rule 20)                │
│ └─ Replace hot path code                               │
└─────────────────────────────────────────────────────────┘
                         ↓
┌─────────────────────────────────────────────────────────┐
│ PEAK PERFORMANCE                                         │
│ ├─ Specialized + Inlined + Vectorized code             │
│ ├─ Data structure optimization                         │
│ └─ Platform-specific tuning                            │
└─────────────────────────────────────────────────────────┘
```

---

## Recommended Implementation Priority

### Phase 1: Foundation ()
1. Lexer + Parser (Rules 7–15, 8–22)
2. Type Checker (Rules 18–22, 49)
3. Basic Codegen (Rules 42–59, 66)

### Phase 2: Core Functionality
1. Module System (Rules 5–6)
2. Functions & Entities (Rules 66–73)
3. Memory Management (Rules 4, 47, 70)

### Phase 3: Runtime + 
1. Virtual Machine (Rules 42–65)
2. Profiler + Hot Spot Detection
3.  Compiler (Specialize rules 24, 35, 56–59, 78–86)

### Phase 4: Advanced
1. Query Optimizer (Rule 78)
2. ML Model Integration (Rule 79)
3. Service Mesh (Rule 80)

---

## Key Design Principles

✅ **Fast Startup**: All  rules → Baseline binary without  latency  
✅ **Peak Performance**:  specializes hot rules 24–35, 56–59, 78–86  
✅ **Safety**:  verification (Rule 76) + Runtime checks where needed  
✅ **Adaptivity**:  rules (23, 35–41, 42–65, 66, 82, 87) optimize based on actual behavior  

---

