/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:    DGTL (Deep General-purpose Transformation Layer)
 * Owner:      Divyanshu Tata
 * Repository: github.com/TataG-Systems/DGTL
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

# MYZON / SOVEREIGN v1.0.0 Full Implementation Spec

## 1. Mission and Non-Negotiables

MYZON must ship as a production language frontend targeting DGTL as the backend.

Success means all of the following are true:
- The full canonical EBNF in `MYZON_SOVEREIGN_v1_1_0_Canonical_EBNF_Debugged.txt` is implemented.
- The compiler pipeline is deterministic and testable end to end.
- Performance is measured and improved continuously against strong external baselines.
- Safety and verification semantics (`verify`, `check`, `isolated`, `secure`, `sealed`) are first-class, not decorative.
- The backend contract with DGTL is explicit, stable, and benchmarkable.

The phrase "number one" is treated as an engineering objective with measurable gates, not a slogan.

## 2. Product Scope

This spec covers:
- Lexer
- Parser
- Pratt expression engine
- AST and typed semantic IR
- Semantic analyzer (user requested "symentic analyser")
- Lowering from typed IR to DGTL
- Runtime contracts for concurrency, services, foreign bindings, and verification
- Benchmark and release gates

Out of scope for this document:
- Package manager design (covered by DGPM specs)
- IDE/LSP protocol details
- Full runtime implementation internals (specified as contracts only)

## 3. Compiler Architecture

## 3.1 Pipeline

1. Source files -> UTF-8 decoding and file identity map.
2. Lexing -> token stream with trivia and diagnostic spans.
3. Parsing -> untyped AST with recovery.
4. Name and module binding -> symbol graph.
5. Type and effect inference/checking -> typed AST.
6. Ownership/concurrency/policy verification -> semantic constraints.
7. Typed AST -> MIR (MYZON Intermediate Representation).
8. MIR -> DGTL IR lowering through frontend bridge APIs.
9. DGTL optimization + validation + emission.
10. Artifact packaging + benchmark validation.

## 3.2 Process Model

- Frontend executable: `myzonc`.
- Backend library/process: DGTL surfaces in `include/dgtl/core/*`, `include/dgtl/ir/*`, `include/dgtl/pipeline/*`, `include/dgtl/isa/*`.
- Optional split mode: frontend writes binary MIR plan; backend consumes and emits target.

## 3.3 Determinism Rules

- Deterministic diagnostics ordering by file id then source position.
- Deterministic hash for AST, MIR, and DGTL module snapshots.
- Stable symbol IDs by declaration order within deterministic module traversal.

## 4. Lexer Spec

## 4.1 Token Kinds

Token classes:
- Identifiers and keywords (including ownership and concurrency keywords from grammar rules 16 and 17)
- Literals (`number`, `string`, `raw_string`, `hex_addr`, booleans, null)
- Operators and punctuation (all symbols in grammar including `|>`, `??`, `??=`, `**`, `<->`, `=>`)
- Structural markers (`{}`, `[]`, `()`, commas, semicolons, colons)
- Attribute marker `@`
- Shell marker `$`
- Doc string token `DOC_STRING`

## 4.2 Lexing Requirements

- Maximal munch for multi-char operators.
- Fast path for ASCII identifiers and numbers.
- Unicode support in string literals per grammar rule 12.
- Precise error messages for malformed escapes and unterminated literals.
- Trivia retention for formatter and doc extraction.

## 4.3 Lexer Data Structures

```text
Token {
  kind: TokenKind
  lexeme_offset: u32
  lexeme_len: u32
  line: u32
  col: u32
  file_id: u32
  flags: bitset
}
```

- Store source in immutable rope/arena slices.
- Keep token buffer contiguous for parser cache locality.

## 5. Parser and Pratt Spec

## 5.1 Parser Strategy

- Recursive descent for declarations/statements.
- Pratt parser for all expression forms in grammar section V.
- Explicit recovery sets for `;`, `}`, `)`, `]`, `,`.

## 5.2 Pratt Binding Powers

From lowest to highest:
1. Pipeline `|>`
2. Logic OR `||`
3. Logic AND `&&`
4. Equality and membership/cast family `== != is in as matches`
5. Relational `>= <= > <`
6. Additive `+ -`
7. Multiplicative `* / % @ ??`
8. Power `**` (right-associative)
9. Prefix unary `! ~ - ++ -- new grad typeof & * comptime`
10. Postfix chain `. () [] ? !`
11. Primary

Parser invariants:
- `power` must parse right-associative.
- Postfix operators can chain arbitrarily.
- Lambda and query lib forms are parsed as primaries.

## 5.3 Parse Functions (Mandatory)

Top level:
- `parse_program`
- `parse_top_stmt`
- `parse_core_stmt`
- `parse_library_stmt`

Declarations:
- `parse_global_binding`
- `parse_import_stmt`
- `parse_platform_stmt`
- `parse_routine`
- `parse_entity_def`
- `parse_trait_def`
- `parse_enum_def`
- `parse_partition_stmt`
- `parse_comptime_block`

Statements:
- `parse_statement`
- `parse_binding`
- `parse_flow`
- `parse_loop`
- `parse_concurrent_op`
- `parse_try_block`
- `parse_verify_block`
- `parse_asm_block`
- `parse_shell_stmt`

Expressions:
- `parse_expression_pratt(min_bp)`
- `parse_prefix`
- `parse_postfix_suffix`
- `parse_primary`
- `parse_lambda`
- `parse_collection_lit`
- `parse_dsa_lit`
- `parse_query_lib`

## 5.4 Error Recovery

- Panic mode at declaration and statement boundaries.
- Insert synthetic missing tokens in AST for continuation.
- Never lose parser state after one file error burst.

## 6. AST Design

## 6.1 AST Layers

- CST-light node layer (optional) for tooling.
- Core AST for semantic analysis.
- Typed AST annotations after type/effect pass.

## 6.2 Core Node Families

- `ProgramNode`
- `DeclNode` variants: global/import/platform/routine/entity/trait/enum/partition/library
- `StmtNode` variants: binding/assignment/flow/loop/concurrent/try/yield/sys/shell/asm/verify/expression
- `ExprNode` variants: literals/identifier/unary/binary/pipeline/call/index/member/lambda/query/collection/dsa/match arm expr
- `TypeNode` variants: primitive/named/generic/array-indexed/nullable
- `AttrNode` and `DocNode`

## 6.3 Memory and IDs

- Arena-allocated immutable nodes.
- Stable `NodeId` for each AST node.
- Side tables keyed by `NodeId` for:
  - source span
  - inferred type
  - ownership/effect qualifiers
  - constant value

## 7. Semantic Analyzer Spec

## 7.1 Pass Order

1. Module graph build and import resolution.
2. Symbol declaration pass.
3. Name resolution pass.
4. Type construction and generic parameter binding.
5. Type checking and inference.
6. Ownership/aliasing rules.
7. Concurrency/effect checking.
8. Verify/check policy enforcement.
9. Constant folding and compile-time evaluation eligibility.
10. MIR generation readiness checks.

## 7.2 Type System Requirements

- Full support for grammar `type_spec`, including nested generic parameters, index modifiers, and nullable suffix.
- `auto` inference with deterministic fallback errors.
- `self` valid only in entity/trait contexts.
- Explicit type rules for DSA and collection literals.

## 7.3 Ownership and Concurrency Rules

- Track qualifiers: `owned`, `shared`, `mut`, `atomic`, `isolated`, `linear`, `managed`, `sealed`, etc.
- Enforce non-copy semantics for `linear` values.
- Enforce safe transfer rules for `spawn`, `thread`, `send`, `recv`.
- Reject data races when mutable alias escapes across concurrency boundary unless `atomic` or synchronized by contract.

## 7.4 Verification and Security Semantics

- `verify { ... }` expressions must be boolean-typed or policy-typed predicates.
- `check` guards on routines/entities must be proven at compile time when marked strict mode, otherwise runtime guard insertion.
- `foreign ... verify <hex>` requires ABI signature and hash contract match.
- `gateway ... secure` requires validated policy block and transport profile.

## 7.5 Semantic Diagnostics

Each diagnostic includes:
- code (e.g., `MYZON-E2104`)
- severity
- primary span
- optional labels and fix hints
- deterministic render format for CI parsing

## 8. MIR (MYZON Intermediate Representation)

## 8.1 Why MIR Exists

MIR decouples language semantics from DGTL opcode details.

MIR holds:
- typed SSA-like values
- structured control flow blocks
- effect and ownership annotations
- high-level operations for match/select/try/concurrency/policy

## 8.2 MIR Requirements

- One MIR function per source routine/lambda body.
- Canonicalized expression forms.
- Explicit basic blocks and terminators.
- Explicit phi-like merge values.
- Explicit call ABI descriptors for internal and foreign calls.

## 9. Lowering to DGTL

Lowering must use DGTL frontend boundary APIs described in `DGTL_FRONTEND_INTEGRATION_SPEC.md`.

## 9.1 Lowering Contracts

- One `DGTLFunction` per MIR function.
- `FrontendLoweringContext` used for expression and statement lowering.
- `DGTLControlFlowGraph` used for block creation, edge linking, sealing, and phi placement.
- `DGTLDebugStore` populated with both source and type location channels.
- `DGTLABIRegistry` used for call signatures and foreign bindings.

## 9.2 Mapping Rules (Core)

- MIR constants -> `LOAD_IMM` and typed metadata.
- Arithmetic/logical operations -> DGTL arithmetic/logical opcodes with exact type tags.
- Branches/match/select -> CFG blocks plus conditional jumps and merge nodes.
- Loops -> canonical loop header/body/latch/exit block pattern.
- Calls -> ABI-checked call nodes.
- Async/concurrency primitives -> runtime shim calls initially, with metadata flags for future backend specialization.
- `verify/check` -> guard nodes, trap/panic contracts, and optional compile-time discharge markers.

## 9.3 Lowering Invariants

- Unique result IDs per function.
- `INVALID_SLOT` for absent operands.
- Transaction boundaries respected during batch emission.
- IR validation after lowering and after each optimization stage in strict mode.

## 10. Runtime Contracts Required for Full Grammar

To implement full language semantics, runtime must provide stable APIs for:
- task scheduling (`spawn`, `async`, `await`, `join`)
- synchronization (`lock`, `release`, `barrier`, `fence`)
- channels (`send`, `recv`, typed message transport)
- panic/retry/finally mechanics
- secure service/gateway transport primitives
- policy enforcement callbacks

Initial implementation can use runtime calls; later phases can inline/specialize hot paths.

## 11. Performance Plan for "Number One" Objective

## 11.1 Principle

No claims without benchmarks. Every optimization must improve measured workloads while preserving correctness.

## 11.2 Performance Tiers

Tier 0 (Correctness foundation):
- Full grammar accepted.
- Strict semantic correctness and deterministic IR.

Tier 1 (Compiler throughput):
- Frontend lex+parse+semantic throughput competitive on large codebases.
- Incremental compile path with dependency graph invalidation.

Tier 2 (Runtime/codegen competitiveness):
- Benchmarks against C++, Rust, Zig, Go, Java, Mojo-equivalent kernels where available.
- Multiple domains: numeric kernels, graph/data workloads, async services, memory-intensive tasks.

Tier 3 (Leadership):
- Beat baseline medians on selected flagship workloads with reproducible setup.
- Publish methodology, flags, machine specs, and confidence intervals.

## 11.3 Benchmark Rules

- Pin hardware, OS, compiler flags, and data seeds.
- Run warm-up and steady-state rounds.
- Use p50/p90 and stddev reporting.
- Require correctness oracle for each benchmark.
- Track regressions per commit in CI.

## 11.4 Optimization Focus Areas

- Frontend hot paths: tokenization, Pratt dispatch, symbol lookup, type solver hotspots.
- Mid-end: MIR canonicalization, effect-aware simplification.
- DGTL backend: pass scheduling, vectorization/polyhedral opportunities, emission quality,  profile feedback.
- Runtime: allocator strategy, task scheduler overhead, channel contention mitigation.

## 12. Testing and Verification Strategy

## 12.1 Test Layers

- Lexer unit tests from grammar token corpus.
- Parser golden tests from EBNF rule coverage.
- Pratt precedence tests for every operator family.
- AST shape tests and serialization snapshots.
- Semantic pass tests for typing, ownership, concurrency, and policy errors.
- MIR and DGTL lowering differential tests.
- End-to-end executable tests.
- Fuzzing for lexer/parser and selected semantic edge cases.

## 12.2 Grammar Coverage Matrix

Maintain a machine-readable matrix:
- One entry per grammar rule (1..88).
- Parse support status.
- Semantic support status.
- Lowering support status.
- Test references.

Release cannot be marked "full implementation" unless every rule is green.

## 13. Milestone Plan

M1 Foundation:
- Lexer + parser + diagnostics + AST for full grammar.

M2 Semantic Core:
- Name/type/generic system + ownership and effect checks.

M3 MIR and Lowering:
- MIR generation and DGTL lowering for all core and library constructs.

M4 Runtime and Advanced Semantics:
- Concurrency, verify/check, policy/service/gateway contracts fully wired.

M5 Performance Strike:
- Benchmark campaign, profile-guided optimization, release gate hardening.

## 14. Team Execution Requirements

- No feature merges without tests and benchmark impact notes.
- Every semantic rule must cite grammar rule IDs.
- Every lowering rule must specify DGTL contract surface used.
- CI must run strict build, full tests, and benchmark smoke set.

## 15. Immediate Build Order (First 6 Weeks)

Week 1:
- Token model, lexer skeleton, keyword/operator table.

Week 2:
- Top-level parser + declaration parser.

Week 3:
- Pratt engine + expression tests.

Week 4:
- Statement/control-flow parser + recovery matrix.

Week 5:
- Symbol table + name resolution + basic typing.

Week 6:
- MIR prototype + first DGTL lowering path (`fn`, expressions, `if`, loops, calls).

## 16. Deliverables

- `myzonc` frontend executable.
- Grammar coverage report.
- Semantic diagnostic catalog.
- MIR spec and schema.
- DGTL lowering conformance test suite.
- Benchmark report with reproducible scripts.

## 17. Final Engineering Position

MYZON can target absolute top-tier performance and developer power, but that requires disciplined implementation and measured proof. This spec defines the full path: implement the entire grammar, enforce semantics rigorously, lower cleanly into DGTL, and win by benchmarks that are reproducible and strict.
