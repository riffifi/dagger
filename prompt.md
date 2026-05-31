# Dagger Compiler Implementation Prompt

You are implementing the **Dagger programming language** compiler and toolchain from the official specification below.

Specification source:
`spec.md`

You must treat the specification as authoritative. The implementation should follow the spec as closely as possible, especially regarding:

* syntax
* semantics
* ownership model
* routing semantics
* type inference
* assembly lowering
* stdlib behaviour
* compiler stages

The language is implemented in **modern C++ (C++20 minimum)**.

---

# Project Goal

Implement a full native compiler toolchain for Dagger:

* lexer
* parser
* AST
* semantic analysis
* type inference
* Stream IR (SIR)
* register allocation
* x86-64 code generation
* assembler/linker integration
* standard library
* module system
* CLI compiler (`dagc`)

Target:

* x86-64 Linux first
* ELF output first
* Intel syntax assembly preferred internally

The implementation must prioritise:

* correctness
* predictable lowering
* explicitness
* readable architecture
* maintainability
* minimal hidden behaviour

The language philosophy matters:
Dagger is “assembly with humane syntax”.

---

# CRITICAL IMPLEMENTATION RULES

## 1. DO NOT TREAT STDLIB FUNCTIONS AS KEYWORDS

This is extremely important.

Functions like:

* `out.write`
* `text.len`
* `math.sqrt`
* `mem.alloc`
* `std.collections.list.push`

are NOT parser keywords.

They are ordinary namespaced functions resolved through the module system and stdlib.

Example:

```dagger
"hello" -> out.write
```

`out.write` is:

* an identifier
* resolved from stdlib/module namespace
* lowered like any other function call

It is NOT syntax.

The parser must only recognise:

* actual language syntax
* reserved keywords/operators

Everything else is symbol resolution.

---

## 2. BUILTINS VS STDLIB

Separate clearly:

### Compiler builtins

Language primitives:

* routing operator
* fork
* loop
* probe/burst
* blocks
* directives
* type syntax
* optional/error types
* inline/comptime
* references
* slices
* register pinning

These are compiler features.

### Standard library

Everything else:

* I/O
* text utilities
* memory allocators
* formatting
* collections
* math helpers

These are ordinary modules.

---

## 3. KEEP LOWERING EXPLICIT

Never generate hidden behaviour.

No:

* hidden allocations
* hidden temporaries unless necessary
* hidden destructor logic
* hidden runtime

Every generated instruction must be explainable from source.

---

# REQUIRED PROJECT STRUCTURE

Use a clean layered compiler architecture.

Recommended structure:

```txt
dagger/
├── compiler/
│   ├── lexer/
│   ├── parser/
│   ├── ast/
│   ├── semantic/
│   ├── types/
│   ├── sir/
│   ├── lowering/
│   ├── regalloc/
│   ├── codegen/
│   ├── linker/
│   └── cli/
│
├── stdlib/
│   ├── std/
│   │   ├── io.dag
│   │   ├── mem.dag
│   │   ├── math.dag
│   │   ├── text.dag
│   │   ├── fmt.dag
│   │   ├── collections/
│   │   └── sys/
│
├── tests/
├── examples/
└── docs/
```

---

# IMPLEMENTATION ORDER

Implement in this exact order.

---

# PHASE 1 — LEXER

Implement:

* token stream
* comments
* literals
* identifiers
* directives
* operators
* punctuation

Important:

* dotted identifiers must lex as a single identifier token:
  `out.write`
  `std.mem.alloc`

Do NOT split dots into member operators.

Dagger namespaces are lexical identifiers.

---

# PHASE 2 — PARSER

Implement recursive descent parser.

AST nodes required:

## Expressions

* literals
* identifiers
* route expressions
* composition expressions
* function calls
* block expressions
* fork expressions
* loops
* each
* unary operators
* references
* dereferences

## Statements

* stream declarations
* function declarations
* type declarations
* directives
* route statements

---

# PHASE 3 — TYPE SYSTEM

Implement:

## Primitive types

* int family
* uint family
* float family
* bool
* byte
* text
* char
* null

## Composite types

* block[N]
* slice[T]
* references
* structs
* unions
* fn(...)
* optional
* error types

---

# PHASE 4 — SYMBOL RESOLUTION

Implement namespaces/modules.

Critical:

* `out.write`
* `text.len`
* `math.sqrt`

must resolve exactly like ordinary symbols.

Resolution order:

1. local scope
2. imported symbols
3. module namespaces
4. stdlib

---

# PHASE 5 — SEMANTIC ANALYSIS

Implement:

* type inference
* route validation
* burst tracking
* ownership validation
* scope validation
* block lifetime analysis
* function signature checking
* union matching validation
* error propagation checking

Important:
burst/probe semantics are core language semantics.

Track:

* live streams
* consumed streams
* moved values

---

# PHASE 6 — STREAM IR (SIR)

Implement an SSA-like IR.

Every value must have:

* explicit lifetime
* explicit ownership
* explicit storage class
* explicit free point

SIR should flatten routing chains.

Example:

```dagger
x -> add(1) -> mul(2) -> out.write
```

becomes sequential IR ops.

---

# PHASE 7 — REGISTER ALLOCATION

Implement graph colouring allocator.

Support:

* register pinning
* spill slots
* stack allocation
* ABI correctness

Target:
System V AMD64 ABI.

---

# PHASE 8 — CODE GENERATION

Generate:

* x86-64 assembly
* ELF object files initially

Use:

* Intel syntax internally

Support:

* arithmetic
* branches
* loops
* calls
* syscalls
* stack frames
* heap ops
* structs
* slices
* unions

---

# PHASE 9 — STDLIB

Implement stdlib IN DAGGER where possible.

The stdlib should compile like normal user code.

Important:
The stdlib is not magical.

Examples:

## out.write

Should ultimately lower to:

* Linux write syscall
  OR
* libc write/printf wrapper

But exposed as ordinary Dagger functions.

---

# LANGUAGE SEMANTICS TO PRIORITISE

---

# ROUTING OPERATOR

This is the core language primitive.

Routing chains are first-class syntax.

Maintain left-to-right semantics.

---

# PROBE VS BURST

This is fundamental.

Implement:

* compile-time burst tracking
* use-after-burst errors
* ownership state machine

---

# BLOCK LIFETIMES

Blocks own stack allocations.

Compiler must:

* compute stack frame sizes
* reclaim stack memory predictably
* emit deterministic frame setup/teardown

---

# ERROR TYPES

Implement tagged success/error representation.

Recommended representation:

```cpp
struct ErrorValue {
    bool is_error;
    union {
        SuccessValue ok;
        TextValue error;
    };
};
```

But optimise layout later.

---

# OPTIONAL TYPES

Represent as:

* tagged nullable
  OR
* nullable pointer optimisation

---

# REFERENCES

References are unsafe by design.

No borrow checker.

Do not add Rust-like restrictions.

---

# MODULE SYSTEM

Every `.dag` file is a module.

Implement:

* imports
* selective imports
* exports
* private symbols
* namespaced lookup

---

# COMPTIME

Implement a separate compile-time evaluator.

Comptime execution must:

* only allow compile-time values
* run before lowering
* substitute constants into AST/SIR

---

# CLI

Compiler executable:
`dagc`

Support flags from spec.

Initial milestones:

* `--check`
* `-S`
* `-o`

---

# TESTING REQUIREMENTS

Implement:

* lexer tests
* parser tests
* semantic tests
* codegen tests
* stdlib tests
* integration tests

Every spec example should become a test case.

---

# ERROR REPORTING

Must follow spec format exactly:

```txt
file:line:col error: message
```

Include:

* source excerpt
* caret
* hint if possible

Good diagnostics matter.

---

# INTERNAL REPRESENTATION GUIDELINES

Prefer:

* tagged unions (`std::variant`)
* arenas for AST allocation
* immutable type objects
* explicit ownership

Avoid:

* giant inheritance trees
* hidden mutation
* overly dynamic runtime structures

---

# RECOMMENDED C++ LIBRARIES

Allowed:

* STL
* fmt
* Catch2 or GoogleTest
* LLVM only optionally for assembler/linker integration

Avoid making LLVM the language runtime.

Dagger should remain:

* simple
* direct
* understandable

---

# IMPORTANT PARSER NOTES

Remember:

```dagger
out.write
```

is IDENTIFIER syntax.

NOT:

* member access
* AST field access
* special syntax

The dot belongs inside the identifier token.

This is critical to preserving Dagger’s namespace philosophy.

---

# CODE STYLE

Prefer:

* explicit code
* small focused files
* descriptive names
* deterministic behaviour

Avoid:

* macro-heavy designs
* template metaprogramming abuse
* overengineering

---

# FINAL GOAL

A user should be able to write:

```dagger
"hello" -> out.write
```

and the compiler should:

1. lex it
2. parse routing
3. resolve `out.write`
4. type-check text -> fn(text -> null)
5. lower to SIR
6. allocate registers
7. emit assembly
8. assemble/link
9. produce native executable

with:

* no runtime
* no GC
* no hidden allocations
* predictable assembly

Implement the compiler incrementally, validating every phase with tests before continuing.

The specification is the source of truth.
