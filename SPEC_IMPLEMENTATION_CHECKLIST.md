# Dagger Spec Implementation Checklist

This file tracks implementation status against `spec.md`.

Status rules used here:
- `[x]` implemented in the current codebase in a usable way
- `[ ]` not implemented yet, or only partially implemented

Notes:
- The current project is still an interpreter-centric prototype, not a production compiler/backend.
- Some items below are parsed or approximated, but if the full spec semantics are not present, they remain unchecked.

## 1. Lexical Structure

- [x] Single-line comments with `#`
- [x] Whitespace-insensitive parsing
- [x] Identifiers with letters, digits, `_`, and `.`
- [x] Integer literals: decimal
- [x] Integer literals: hex (`0x`)
- [x] Integer literals: binary (`0b`)
- [x] Integer literals: octal (`0o`)
- [x] Float literals
- [x] Float exponent literals (`1.0e10`)
- [x] Text literals with basic escapes
- [x] Character literals with basic escapes
- [x] Boolean literals
- [x] `null` literal
- [x] `->` route operator
- [x] `=>` function result type syntax
- [x] `::` type bind syntax
- [x] `=` initialization syntax
- [x] `?` probe syntax
- [x] `!` burst syntax
- [x] Probe comparison tokens (`?=`, `?!=`, `?>`, `?>=`, `?<`, `?<=`)
- [x] `_` wildcard syntax
- [x] `,` multi-stream separator
- [ ] `..` range syntax
- [ ] `[n]` offset access syntax
- [ ] `&` reference syntax
- [ ] `*` dereference syntax
- [x] Keywords currently recognized for implemented subset: `true`, `false`, `null`, `fork`, `block`, `loop`
- [ ] Full keyword/directive surface from spec (`loop`, `@burst`, `@pin`, `@extern`, `@inline`, `@comptime`, etc.)

## 2. Core Concepts

- [x] Stream declaration syntax with `~`
- [x] Function declaration syntax with `@fn`
- [x] Field/block syntax with `[ ... ]`
- [x] Top-level execution model for `.dag` source
- [x] Left-to-right route chaining

## 3. Streams

- [x] Variable declaration with explicit type
- [x] Variable declaration with inferred type/value
- [x] Uninitialized stream declaration syntax
- [x] Probe read syntax
- [x] Burst free semantics after `!x` use
- [x] Re-routing into an existing stream (`x -> ... -> x`)
- [x] Multi-stream declaration (`~ a, b, c = ...`)
- [ ] Register pinning (`@rax`, etc.)
- [ ] Real static/data-section semantics for `@static`

## 4. Types

- [ ] Full type system and static type checking
- [x] Runtime value support for primitive-ish values: `int`, `float`, `bool`, `text`, `char`, `null`
- [ ] Full primitive type set from spec (`int8`, `uint64`, `float32`, `byte`, etc.)
- [ ] `block[n]`
- [ ] `slice[T]`
- [x] Struct type declaration with `@type Name [ ... ]`
- [x] Struct/object literal syntax (`[ x = 1, y = 2 ]`)
- [x] Dotted field access (`p.x`, nested `state.pos.x`)
- [ ] Union shapes
- [ ] Function types / first-class typed function values
- [ ] Optional type semantics (`int?`)
- [ ] Error type semantics (`int!err`)
- [ ] Type inference beyond the current runtime-friendly subset
- [ ] Explicit cast functions (`cast`, `cast.unsafe`) with real type behavior

## 5. Functions

- [x] Function declarations
- [x] Function parameter declarations
- [x] Function result type syntax
- [x] Function body as block/expression
- [x] Multiple function inputs
- [ ] Multiple outputs (`=> [int, int]`)
- [x] Recursive functions
- [ ] `@inline` semantics
- [ ] First-class functions as values
- [ ] Function composition (`>>`)
- [ ] Partial application as a distinct language feature

## 6. Routing Operator

- [x] Basic routing
- [x] Chained routing
- [x] Routing into a new captured stream (`-> ~ result`)
- [x] Routing into an existing stream
- [x] Routing into a block
- [x] Routing multiple streams into a function
- [x] Routing into `_` to discard output
- [ ] Real `tee(fn)` side-route semantics

## 7. Control Flow

- [x] `fork` branching
- [x] Wildcard `_` arm
- [x] Probe-comparison conditions inside `fork`
- [x] Fork returning a value
- [ ] Warning on non-exhaustive fork
- [x] `loop`
- [ ] `loop.range`
- [ ] `each`
- [ ] `@break`
- [ ] `@skip`

## 8. Memory Model

- [x] Stack allocation model (represented in SIR)
- [ ] Heap allocation annotations (`@heap`)
- [x] Explicit free with `!name` or scope exit (represented in SIR)
- [x] Scope-exit cleanup analysis
- [ ] Copy vs move semantics
- [ ] References (`&x`)
- [ ] Dereference function / `*ref`
- [ ] Slices as pointer+length views

## 9. Blocks (Scope)

- [x] Basic block expressions
- [x] Blocks as expressions
- [ ] Named blocks such as `block.setup`
- [x] Block input binding (`x -> block [~ n] [ ... ]`) or bracket form

## 10. Built-in Functions

### Arithmetic
- [x] `add`
- [x] `sub`
- [x] `mul`
- [x] `div`
- [x] `mod`
- [x] `neg`
- [ ] `abs`
- [ ] `min`
- [ ] `max`
- [ ] `pow`

### Bitwise
- [ ] `bit.and`
- [ ] `bit.or`
- [ ] `bit.xor`
- [ ] `bit.not`
- [ ] `bit.shl`
- [ ] `bit.shr`
- [ ] `bit.sar`

### Comparison
- [x] `eq`
- [x] `neq`
- [x] `gt`
- [x] `lt`
- [x] `gte`
- [x] `lte`

### Logic
- [x] `and`
- [x] `or`
- [x] `not`

### Text
- [x] `text.len`
- [ ] `text.at`
- [ ] `text.slice`
- [x] `text.join`
- [ ] `text.find`
- [ ] `text.trim`
- [ ] `text.split`
- [x] `text.from`

### I/O
- [x] `out.write`
- [x] `out.writeln`
- [x] `out.write_err`
- [ ] `in.read`
- [ ] `in.read_raw`
- [ ] `file.open`
- [ ] `file.read`
- [ ] `file.write`
- [ ] `file.close`

### Memory
- [ ] `mem.alloc`
- [ ] `mem.realloc`
- [ ] `mem.free`
- [ ] `mem.copy`
- [ ] `mem.zero`

### Math
- [ ] `math.sqrt`
- [ ] `math.floor`
- [ ] `math.ceil`
- [ ] `math.round`
- [ ] `math.sin`
- [ ] `math.cos`
- [ ] `math.tan`
- [ ] `math.log`
- [ ] `math.log2`
- [ ] `math.log10`

### Utility
- [ ] `cast`
- [ ] `cast.unsafe`
- [ ] `tee`
- [ ] `tap`
- [x] `id`
- [x] `const`
- [x] `assert`
- [ ] `unreachable`

## 11. Error Handling

- [ ] Error types (`!err`)
- [ ] `@error(...)` as a typed error-producing language feature
- [ ] `@ok` / `@err` fork matching
- [ ] `@bubble`
- [ ] `@or(...)`

## 12. Modules

- [x] File-as-module semantics
- [x] `@use module`
- [ ] Selective import
- [ ] Export control
- [ ] External C ABI declarations with `@extern`

## 13. Compile-time Functions (Macros)

- [ ] `@comptime`
- [ ] Macro/compile-time function evaluation
- [ ] Compile-time branching/generation features from the spec

## 14. Assembly Mapping / Backend

- [ ] Stream-to-register lowering
- [ ] Arithmetic lowering to x86-64
- [ ] Function call lowering
- [ ] Fork lowering
- [ ] Loop lowering
- [ ] Block entry/exit lowering
- [ ] `@burst` lowering
- [ ] System call lowering

## 15. Grammar Coverage

- [x] Basic parser for streams, functions, blocks, fork, routing, literals
- [x] Parser support for multi-input routes
- [x] Parser support for struct types and struct literals
- [ ] Full grammar parity with spec EBNF

## 16. Standard Library

- [ ] `std.io`
- [ ] `std.mem`
- [ ] `std.math`
- [ ] `std.text`
- [ ] `std.collections`
- [ ] `std.sys`
- [ ] `std.fmt`

## 17. Compiler Pipeline

- [x] Stage 1: Lex
- [x] Stage 2: Parse
- [x] Stage 3: Type Resolution (Semantic Analysis)
- [x] Stage 4: Stream Lowering (SIR)
- [ ] Stage 5: Register Allocation
- [ ] Stage 6: Code Generation
- [ ] Compiler flags from spec

## 18. Examples / End-to-End Coverage

- [x] Hello world-style program
- [x] Recursive factorial-style program
- [ ] FizzBuzz example
- [ ] Fibonacci example
- [ ] File I/O example
- [ ] Struct and function composition example
- [ ] Manual memory and system call example

## 19. Quality / Production Readiness Tasks Implied By The Spec

- [x] C++ test harness
- [x] `.dag` golden/integration tests
- [ ] Much broader spec coverage tests
- [ ] Error-reporting improvements with precise diagnostics
- [x] Static semantic analysis pass
- [ ] Stable module system
- [ ] FFI sufficient for OS and GUI bindings
- [ ] Native code generation
- [ ] Packaging / build flow for real applications
- [ ] Documentation examples kept in sync with executable tests

## Short Summary

Implemented well enough to check:
- Lexing/parsing for the core language subset
- Stream declarations and rerouting
- Function declarations with multiple inputs and recursion
- Fork control flow with probe comparisons
- Struct types, struct literals, dotted field access, and basic loop execution
- A small set of built-in functions
- C++ and `.dag` automated tests

Still major missing areas:
- Memory model
- Error model
- Modules / FFI
- Full type system
- Standard library
- Native x86-64 backend
- App-building platform integration such as GUI bindings
