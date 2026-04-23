# Dagger Language Specification
**Version:** 0.1.0-draft  
**Target:** x86-64 assembly (ELF/PE/Mach-O)  
**File extension:** `.dag`  
**Tagline:** Sharp. Direct. No runtime.

---

## Table of Contents

1. [Overview](#1-overview)
2. [Design Philosophy](#2-design-philosophy)
3. [Lexical Structure](#3-lexical-structure)
4. [Core Concepts](#4-core-concepts)
5. [Variables and Values](#5-variables-and-values)
6. [Types](#6-types)
7. [Functions](#7-functions)
8. [Routing Operator](#8-routing-operator)
9. [Control Flow](#9-control-flow)
10. [Memory Model](#10-memory-model)
11. [Blocks (Scope)](#11-blocks-scope)
12. [Built-in Functions](#12-built-in-functions)
13. [Error Handling](#13-error-handling)
14. [Modules](#14-modules)
15. [Compile-time Functions (Macros)](#15-compile-time-functions-macros)
16. [Assembly Mapping](#16-assembly-mapping)
17. [Grammar (EBNF)](#17-grammar-ebnf)
18. [Standard Library](#18-standard-library)
19. [Compiler Pipeline](#19-compiler-pipeline)
20. [Examples](#20-examples)

---

## 1. Overview

Dagger is a systems programming language that compiles directly to x86-64 assembly with no runtime, no garbage collector, and no hidden cost. It is designed to make the flow of data through a program explicit, readable, and honest about what the hardware is actually doing.

Where most languages model programs as collections of objects or sequences of instructions, Dagger models programs as **values routed through functions inside explicit blocks**. Data flows. Functions transform it. Blocks contain it. This is not a metaphor — it maps directly to how registers, ALU operations, and the call stack work at the silicon level.

Dagger is not a high-level language with an assembly backend. It is an assembly language with a humane syntax.

Terminology note:
- This spec now prefers conventional terms in prose: `function`, `block`, and `type`
- The preferred spellings are `@fn`, `block`, and `@type`
- `loop` is the standard loop keyword; the older `pulse` name is removed from this spec

### Goals

- Every line of Dagger source corresponds to a predictable, auditable sequence of assembly instructions
- No hidden allocations, no implicit copies, no surprise destructor calls
- Readable left-to-right data flow with the `->` routing operator
- Shape inference that requires zero annotations for straightforward code
- Explicit memory ownership without a borrow checker — the programmer is trusted
- Compile to a single flat binary with no dependencies

### Non-goals

- Garbage collection
- A standard runtime or VM
- Object-oriented dispatch (no vtables, no inheritance)
- Exceptions (Dagger uses error shapes instead)
- Cross-platform abstraction (target is x86-64; other architectures are future work)

---

## 2. Design Philosophy

### 2.1 Data Has Direction

In Dagger, data always moves in one direction: left to right through the `->` operator. You do not hide control flow behind implicit dispatch; you route a value through an explicit function. This isn't just aesthetic. It forces the programmer to think about what data is entering a transform and what type it has when it exits.

```dagger
~ score :: int = 72
score -> validate -> rank -> out.write
```

This reads exactly like what the CPU does: load score, pass it to validate, pass the result to rank, write the final output.

### 2.2 Values and Variables

Classical variables are locations. In Dagger, values move through explicit operations. A value has a type, a lifetime, and a consumption model (probe vs burst). This distinction matters because reading a value and consuming it are different assembly operations — Dagger makes you say which you mean.

### 2.3 Explicit Scope

Scope in Dagger is physical. `[ ]` brackets define a block — a spatial region where values exist. When execution leaves a block, everything declared inside it is freed. This maps directly to stack frame entry and exit. There is no garbage collector because there is no garbage — everything has a declared home.

### 2.4 Type Inference Over Type Declarations

Dagger does not require explicit type declarations everywhere. It uses **types** that describe what a value looks like and how it can be routed. Types are inferred from usage wherever possible. You annotate when you want to be explicit or when the compiler cannot infer.

### 2.5 Explicit Is Better Than Magic

If memory is allocated, you can see it. If a copy happens, you caused it. If something is freed, you wrote `@burst` or exited a block. There are no implicit operations in Dagger. Every assembly instruction has a visible reason in the source.

---

## 3. Lexical Structure

### 3.1 Comments

```dagger
# this is a single-line comment
```

There are no multi-line comments. Long explanations belong in documentation, not source.

### 3.2 Whitespace

Whitespace is not significant for parsing (Dagger is not indentation-sensitive) but convention is two-space indentation inside blocks. Newlines separate statements. Multiple statements on one line are separated by `;`.

### 3.3 Identifiers

Identifiers begin with a letter or underscore, followed by any combination of letters, digits, underscores, or dots. Dots are used for namespacing within identifiers (e.g. `out.write`, `mem.alloc`).

```
identifier ::= [a-zA-Z_][a-zA-Z0-9_.]*
```

### 3.4 Literals

```dagger
# integers
42
-7
0xFF        # hex
0b1010      # binary
0o17        # octal

# floats
3.14
-0.5
1.0e10

# text (UTF-8 string)
"hello, world"
"line one\nline two"

# characters
'A'
'\n'

# boolean
true
false

# null stream
null
```

### 3.5 Operators

| Operator | Meaning |
|----------|---------|
| `->` | Route (data flows right) |
| `=>` | Function result type declaration |
| `::` | Type bind |
| `=` | Stream initialisation |
| `?` | Probe (non-consuming read) |
| `!` | Burst (consuming read, immediate free) |
| `?>` `?<` `?>=` `?<=` `?=` `?!=` | Probe comparisons |
| `@` | Gate/directive prefix |
| `_` | Wildcard / catch-all |
| `..` | Range |
| `[n]` | Offset access |
| `,` | Multi-stream separator |
| `&` | Stream reference (borrow address) |
| `*` | Dereference |

### 3.6 Keywords

```
~ @fn @type @burst @pin @static @extern @inline @comptime
loop fork block null true false
```

---

## 4. Core Concepts

### 4.1 The Three Primitives

Everything in Dagger is built from three primitives:

| Primitive | Symbol | What it is |
|-----------|--------|-----------|
| Stream | `~` | A named, shaped flow of data |
| Function | `@fn` | A named transform that accepts and emits values |
| Block | `[ ]` | A spatial scope that owns values |

### 4.2 The Routing Operator

`->` is the single most important operator in Dagger. It routes the output of the left-hand side into the input of the right-hand side. It chains. It composes. It is the spine of every program.

```dagger
source -> transform -> sink
```

### 4.3 Execution Model

A `.dag` file's top-level statements form the program entry. There is no `main()` function. The file is the program. Execution starts at the first statement and follows routing operators forward.

```dagger
# this is a complete, valid Dagger program
"hello, world" -> out.write
```

---

## 5. Variables and Values

### 5.1 Declaration

```dagger
~ name :: type = value
~ name :: type          # uninitialized (must be written before probed)
~ name = value          # type inferred
```

### 5.2 Probe vs Burst

```dagger
~ x :: int = 10

?x              # probe: reads x, x still exists, compiles to: mov rax, [x]
!x              # burst: reads x, x is freed, compiles to: mov rax, [x] ; (stack: adjust sp)
```

Probing a stream does not advance or consume it. Bursting a stream consumes it — after a burst, the stream is gone and any subsequent access is a compile error.

### 5.3 Variable Reassignment By Routing

```dagger
~ x :: int = 5
x -> add(1) -> x    # route x through a function, result back into x
```

Re-routing into the same stream is an in-place update. The compiler emits a single register operation.

### 5.4 Multi-stream Declaration

```dagger
~ a, b, c :: int = 1, 2, 3
```

### 5.5 Register Pinning

```dagger
~ counter :: int @rax    # compiler must store counter in rax
```

Pins a stream to a specific x86-64 register. Useful for system calls, intrinsics, and performance-critical paths. The compiler will error if the register is unavailable at that point.

### 5.6 Static Streams

```dagger
@static ~ pi :: float = 3.14159
```

Stored in the `.data` or `.rodata` section. Exists for the lifetime of the program.

---

## 6. Types

Types describe the contract of a value — what it looks like in memory and what functions it can be routed into. Types are inferred wherever possible.

### 6.1 Primitive Types

| Type | Size | Description |
|-------|------|-------------|
| `int` | 64-bit | Signed integer (default) |
| `int8` `int16` `int32` `int64` | explicit | Sized signed integers |
| `uint` `uint8` `uint16` `uint32` `uint64` | explicit | Unsigned integers |
| `float` | 64-bit | IEEE 754 double |
| `float32` | 32-bit | IEEE 754 single |
| `bool` | 1 byte | true / false |
| `byte` | 8-bit | Raw byte |
| `text` | ptr+len | UTF-8 string slice (pointer + length, no null terminator) |
| `char` | 32-bit | Unicode code point |
| `null` | 0 bytes | Absence of value |

### 6.2 Composite Types

#### Block (fixed-size buffer)
```dagger
~ buf :: block[256]        # 256 bytes on stack
~ buf :: block[256] @heap  # 256 bytes on heap
```

#### Slice (pointer + length)
```dagger
~ items :: slice[int]      # dynamic-length sequence of ints
```

#### Struct type
```dagger
@type Point [
    x :: float
    y :: float
]

~ p :: Point = [ x = 1.0, y = 2.0 ]
p.x -> out.write
```

#### Union type
```dagger
@type Num [
    | int
    | float
]
```

#### Function type
```dagger
~ transform :: fn(int -> int)
```

#### Optional type
```dagger
~ maybe :: int?            # int or null
```

#### Error type
```dagger
~ result :: int!err        # int or an error — see section 13
```

### 6.3 Type Inference

The compiler infers types from:
- Literal values (`42` infers `int`, `3.14` infers `float`, `"x"` infers `text`)
- Function output types
- Routing chain context

When inference is ambiguous, the compiler emits a descriptive error asking for an explicit annotation.

### 6.4 Type Casting

```dagger
~ x :: int = 65
x -> cast(char) -> out.write    # routes x through cast, output type is char
```

Casts are explicit function calls, never implicit. Narrowing casts that could lose data require `cast.unsafe`.

---

## 7. Functions

Functions are the transformation units of Dagger. A function accepts one or more input values, transforms them, and emits an output value. Functions do not "execute" in isolation — data is *routed through* them.

### 7.1 Function Declaration

```dagger
@fn name [inputs] => output_type [
    # body: last expression is the output
]
```

```dagger
@fn double [~ n :: int] => int [
    n -> mul(2)
]
```

The body of a function is a block. The last routed value in the block is the function's output. No `return` keyword.

### 7.2 Multiple Inputs

```dagger
@fn add_scaled [~ a :: int, ~ b :: int, ~ factor :: int] => int [
    b -> mul(factor) -> add(a)
]

3, 4, 2 -> add_scaled -> out.write    # outputs 11
```

### 7.3 Multiple Outputs

```dagger
@fn minmax [~ a :: int, ~ b :: int] => [int, int] [
    a -> min(b),
    a -> max(b)
]

~ lo, hi = 5, 9 -> minmax
```

### 7.4 Recursive Functions

```dagger
@fn factorial [~ n :: int] => int [
    n -> fork [
        ?<= 1  -> 1
        _      -> n -> sub(1) -> factorial -> mul(n)
    ]
]
```

### 7.5 Inline Functions

```dagger
@inline @fn square [~ n :: int] => int [
    n -> mul(n)
]
```

`@inline` instructs the compiler to inline the function body at every call site. No `call` instruction is emitted.

### 7.6 First-class Functions

Functions are values. They can be stored, passed, and routed.

```dagger
~ op :: fn(int -> int) = double

5 -> op -> out.write    # outputs 10
```

### 7.7 Function Composition

```dagger
~ pipeline = double >> square    # compose: double then square
5 -> pipeline -> out.write       # outputs 100
```

`>>` composes two functions into one. The output type of the left function must match the input type of the right function.

### 7.8 Partial Application

```dagger
~ add5 = add(5)    # partially apply add with second arg = 5
3 -> add5          # outputs 8
```

---

## 8. Routing Operator

### 8.1 Basic Routing

```dagger
source -> fn
```

### 8.2 Chained Routing

```dagger
source -> fn_a -> fn_b -> fn_c
```

Left-associative. Each function's output becomes the next function's input.

### 8.3 Routing Into a Stream

```dagger
source -> fn -> ~ result
# or into an existing stream:
source -> fn -> result
```

### 8.4 Routing Into a Block

```dagger
source -> block [
    # source is available here as the block's input value
    # last expression exits the block as output
]
```

### 8.5 Routing Multiple Streams

```dagger
a, b -> fn_that_takes_two
```

### 8.6 Discarding Output

```dagger
source -> fn -> _    # route to wildcard: output discarded
```

### 8.7 Tee Routing (split without consuming)

```dagger
source -> tee(log.write) -> next_fn
```

`tee` probes the value, routes a copy to its argument function, and passes the original forward unchanged.

---

## 9. Control Flow

### 9.1 Fork (Branching)

`fork` routes the input stream into the first matching arm. Arms are checked top to bottom.

```dagger
value -> fork [
    ?= 0    -> "zero"   -> out.write
    ?> 0    -> "pos"    -> out.write
    _       -> "neg"    -> out.write
]
```

Each arm is a probe condition followed by `->` and a routing chain. The `_` arm is the catch-all. If no arm matches and there is no `_`, the compiler emits a warning (not an error — Dagger trusts you).

Fork with no output (all arms are sinks):
```dagger
x -> fork [
    ?> 100  -> handle_overflow
    _       -> process
]
```

Fork with output (all arms must emit the same type):
```dagger
~ label = x -> fork [
    ?>= 90  -> "excellent"
    ?>= 60  -> "passing"
    _       -> "fail"
]
```

### 9.2 Loop

`loop` repeats its body block while its condition expression evaluates to `true`.

```dagger
loop [condition] block [
    # body
]
```

```dagger
~ i :: int = 0

loop [i -> lt(10)] block [
    i -> out.write
    i -> add(1) -> i
]
```

The condition is evaluated on each iteration. The body is a full block — values declared inside are freed on each iteration.

Loop with index:
```dagger
loop.range(0, 10) -> block [~ i] [
    i -> out.write
]
```

`loop.range` is a built-in that emits an int stream incrementing from start to end (exclusive).

### 9.3 Each (Iteration)

Route a slice through `each` to iterate its elements:

```dagger
~ nums :: slice[int] = [1, 2, 3, 4, 5]

nums -> each -> [~ n] [
    n -> mul(2) -> out.write
]
```

### 9.4 Break and Skip

Inside a `loop` or `each` block:

```dagger
loop [i -> lt(100)] block [
    i -> fork [
        ?= 50  -> @break
        _      -> i -> out.write
    ]
    i -> add(1) -> i
]
```

```dagger
# @skip advances to the next iteration
nums -> each -> [~ n] [
    n -> fork [
        ?< 0  -> @skip
        _     -> n -> out.write
    ]
]
```

---

## 10. Memory Model

Dagger has no garbage collector and no implicit allocation. All memory is either stack, static, or explicitly heap-allocated.

### 10.1 Stack Allocation

All values declared in a block live on the stack by default. They are freed when the block exits.

```dagger
~ x :: int = 42           # stack allocated, 8 bytes
~ buf :: block[128]       # stack allocated, 128 bytes
```

The compiler tracks stack frame size at compile time and emits a single `sub rsp, N` on block entry.

### 10.2 Heap Allocation

```dagger
~ buf :: block[1024] @heap    # heap allocated
~ arr :: slice[int] @heap     # heap-allocated slice
```

Heap values must be explicitly freed with `@burst` or they will be freed on scope exit if inside a `block`. The compiler warns on unfreed heap values at the end of their declaring scope.

### 10.3 Explicit Free

```dagger
@burst buf     # frees buf, compiles to call free / or direct dealloc
```

After `@burst`, the stream is gone. Any use after burst is a compile error.

### 10.4 Stack vs Heap Decision Guide

| Use stack when | Use heap when |
|----------------|---------------|
| Size is known at compile time | Size is determined at runtime |
| Lifetime matches the current block | Lifetime outlasts the current block |
| Size is small (< a few KB) | Size is large |

### 10.5 Copies and Moves

```dagger
~ a :: int = 5
~ b = a          # COPY: a and b both exist, compiles to mov rbx, rax

~ c = !a         # MOVE (burst): a is consumed, c gets the value, a is freed
```

Copies of primitive shapes are always cheap (single register move). Copies of blocks or slices copy the entire buffer — use references to avoid this.

### 10.6 References

```dagger
~ x :: int = 10
~ ref = &x           # ref holds the address of x, type: &int

ref -> deref         # dereference: reads value at ref's address
*ref -> out.write    # shorthand dereference
```

References do not have lifetime tracking (unlike Rust borrows). The programmer is responsible for ensuring the referenced stream outlives the reference.

### 10.7 Slices

A slice is a `(pointer, length)` pair. It does not own its memory — it describes a view into an existing buffer.

```dagger
~ buf :: block[256]
~ view :: slice[byte] = buf[0..128]    # slice of first 128 bytes of buf
```

---

## 11. Blocks (Scope)

### 11.1 Basic Block

```dagger
block [
    ~ x :: int = 5
    x -> out.write
]
# x is freed here
```

### 11.2 Blocks as Expressions

A block can produce an output value — the last routed expression in the block is its result:

```dagger
~ result = block [
    ~ a = 3
    ~ b = 4
    a -> add(b)    # this is the block's output
]
result -> out.write    # outputs 7
```

### 11.3 Named Blocks

Blocks can be named for clarity (name has no semantic effect, only documentary):

```dagger
block.setup [
    # initialization
]

block.main [
    # program body
]
```

### 11.4 Block Inputs

```dagger
~ x = 10
x -> block [~ n] [
    n -> mul(2) -> out.write
]
```

The routed value enters the block as the named value `n`.

---

## 12. Built-in Functions

### 12.1 Arithmetic

| Gate | Input | Output | Notes |
|------|-------|--------|-------|
| `add(n)` | num | num | adds n |
| `sub(n)` | num | num | subtracts n |
| `mul(n)` | num | num | multiplies by n |
| `div(n)` | num | num | integer or float division |
| `mod(n)` | int | int | modulo |
| `neg` | num | num | negate |
| `abs` | num | num | absolute value |
| `min(n)` | num | num | minimum of input and n |
| `max(n)` | num | num | maximum of input and n |
| `pow(n)` | num | num | power |

### 12.2 Bitwise

| Gate | Notes |
|------|-------|
| `bit.and(n)` | bitwise AND |
| `bit.or(n)` | bitwise OR |
| `bit.xor(n)` | bitwise XOR |
| `bit.not` | bitwise NOT |
| `bit.shl(n)` | shift left by n |
| `bit.shr(n)` | logical shift right |
| `bit.sar(n)` | arithmetic shift right |

### 12.3 Comparison (returns bool)

| Gate | Notes |
|------|-------|
| `eq(n)` | equal |
| `neq(n)` | not equal |
| `gt(n)` | greater than |
| `lt(n)` | less than |
| `gte(n)` | greater than or equal |
| `lte(n)` | less than or equal |

### 12.4 Logic

| Gate | Notes |
|------|-------|
| `and(b)` | logical AND |
| `or(b)` | logical OR |
| `not` | logical NOT |

### 12.5 Text

| Gate | Input | Output | Notes |
|------|-------|--------|-------|
| `text.len` | text | int | byte length |
| `text.at(n)` | text | char | char at index n |
| `text.slice(a,b)` | text | text | substring |
| `text.join(t)` | text | text | concatenate |
| `text.find(t)` | text | int? | find substring, null if not found |
| `text.trim` | text | text | trim whitespace |
| `text.split(sep)` | text | slice[text] | split by separator |
| `text.from(n)` | num | text | format number as text |

### 12.6 I/O

| Gate | Input | Output | Notes |
|------|-------|--------|-------|
| `out.write` | text | null | write to stdout |
| `out.writeln` | text | null | write with newline |
| `out.write_err` | text | null | write to stderr |
| `in.read` | null | text | read line from stdin |
| `in.read_raw` | null | slice[byte] | read raw bytes |
| `file.open(path)` | text | file!err | open file, returns handle or error |
| `file.read` | file | text!err | read entire file |
| `file.write` | file, text | null!err | write text to file |
| `file.close` | file | null | close file handle |

### 12.7 Memory

| Gate | Notes |
|------|-------|
| `mem.alloc(n)` | allocate n bytes on heap, returns &byte |
| `mem.realloc(ptr, n)` | resize allocation |
| `mem.free(ptr)` | free heap pointer |
| `mem.copy(src, dst, n)` | copy n bytes |
| `mem.zero(ptr, n)` | zero n bytes |

### 12.8 Math

| Gate | Notes |
|------|-------|
| `math.sqrt` | square root |
| `math.floor` `math.ceil` `math.round` | rounding |
| `math.sin` `math.cos` `math.tan` | trig |
| `math.log` `math.log2` `math.log10` | logarithm |

### 12.9 Utility

| Gate | Notes |
|------|-------|
| `cast(type)` | safe type cast |
| `cast.unsafe(type)` | unsafe reinterpret cast |
| `tee(fn)` | probe + side-route, pass through |
| `tap(fn)` | alias for tee |
| `id` | identity function — passes input through unchanged |
| `const(v)` | discards input, emits constant v |
| `assert(cond)` | halts with diagnostic if condition fails |
| `unreachable` | marks a path as impossible; if reached, halts |

---

## 13. Error Handling

Dagger has no exceptions. Errors are types — a function that can fail returns an error type, and the caller must handle it explicitly.

### 13.1 Error Type

```dagger
~ result :: int!err    # either an int or an error
```

The `!err` suffix marks a value as potentially an error.

### 13.2 Producing Errors

```dagger
@fn divide [~ a :: int, ~ b :: int] => int!err [
    b -> fork [
        ?= 0  -> @error("division by zero")
        _     -> a -> div(b)
    ]
]
```

`@error(msg)` produces an error value with a text message.

### 13.3 Handling Errors

```dagger
~ val = 10, 0 -> divide

val -> fork [
    @ok  -> [~ n] [ n -> out.write ]
    @err -> [~ e] [ e -> out.write_err ]
]
```

`@ok` matches the success arm, `@err` matches the error arm.

### 13.4 Propagating Errors

```dagger
@fn safe_sqrt [~ n :: float] => float!err [
    n -> fork [
        ?< 0.0  -> @error("negative input")
        _       -> n -> math.sqrt
    ]
]

@fn compute [~ x :: float] => float!err [
    x -> safe_sqrt -> @bubble    # @bubble propagates error upward if present
    # if safe_sqrt succeeds, routing continues here
]
```

`@bubble` short-circuits: if the value is an error, it exits the current function with that error. If it is `@ok`, routing continues with the unwrapped value.

### 13.5 Default on Error

```dagger
x -> safe_sqrt -> @or(0.0)    # use 0.0 if error
```

---

## 14. Modules

### 14.1 File as Module

Every `.dag` file is a module. Its name is its filename without the extension.

```
src/
  math.dag
  io.dag
  main.dag
```

### 14.2 Importing

```dagger
@use math
@use io

5 -> math.double -> io.out.write
```

`@use` makes the module's exported functions and values available under its name.

### 14.3 Selective Import

```dagger
@use math [ double, square ]

5 -> double -> out.write
```

### 14.4 Exporting

By default, all top-level functions and `@static` values are exported. Prefix with `@private` to hide from importers:

```dagger
@private @fn helper [~ n :: int] => int [
    n -> mul(3)
]

@fn triple [~ n :: int] => int [
    n -> helper
]
```

### 14.5 External (C ABI)

```dagger
@extern @fn printf [~ fmt :: &byte, ...] => int
@extern @fn malloc [~ size :: uint] => &byte
```

`@extern` declares a function with C calling convention. Dagger will emit the correct `call` instruction and handle ABI-compliant argument passing.

---

## 15. Compile-time Functions (Macros)

`@comptime` functions run entirely at compile time. Their output is substituted inline as a constant. They have access to type information and literal values only — no runtime state.

```dagger
@comptime @fn kilobytes [~ n :: int] => int [
    n -> mul(1024)
]

~ buf :: block[@kilobytes(4)]    # compiles to block[4096]
```

Compile-time functions are used for:
- Constant folding
- Computed buffer sizes
- Conditional compilation
- Code generation over types

```dagger
@comptime @fn is_64bit => bool [
    @arch -> eq("x86_64")
]
```

---

## 16. Assembly Mapping

This section documents how every Dagger construct maps to x86-64 assembly. This is the contract — if the output deviates, it is a compiler bug.

### 16.1 Stream to Register Mapping

The compiler uses graph coloring to assign streams to registers. Priority order (callee-saved preferred for long-lived streams):

```
rax rbx rcx rdx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
```

Stack slots are used when register pressure exceeds available registers.

### 16.2 Arithmetic Routing

```dagger
a -> add(b) -> c
```
```asm
mov rax, [a]
add rax, [b]
mov [c], rax
```

### 16.3 Function Call

```dagger
x -> double
```
```asm
mov rdi, [x]    ; first argument
call double
```

### 16.4 Fork

```dagger
x -> fork [
    ?> 0  -> pos_handler
    _     -> neg_handler
]
```
```asm
mov rax, [x]
cmp rax, 0
jg  .pos
jmp .neg
.pos:
  call pos_handler
  jmp .end
.neg:
  call neg_handler
.end:
```

### 16.5 Loop

```dagger
loop [?i < 10] [body]
```
```asm
.loop_start:
  mov rax, [i]
  cmp rax, 10
  jge .loop_end
  ; body
  jmp .loop_start
.loop_end:
```

### 16.6 Block Entry/Exit

```dagger
block [
    ~ x :: int = 5
    ~ buf :: block[64]
]
```
```asm
sub rsp, 72         ; 8 (int) + 64 (block)
mov qword [rsp+64], 5
; body
add rsp, 72
```

### 16.7 @burst

```dagger
@burst heap_buf
```
```asm
mov rdi, [heap_buf]
call free
```

### 16.8 System Calls

```dagger
@syscall write [~ fd :: int, ~ buf :: &byte, ~ len :: uint] => int
```

Syscall functions emit direct `syscall` instructions with the correct register setup per the Linux x86-64 ABI (rax=syscall number, rdi, rsi, rdx, r10, r8, r9 for args).

---

## 17. Grammar (EBNF)

```ebnf
program         ::= statement*

statement       ::= stream_decl
                  | route_stmt
                  | function_decl
                  | type_decl
                  | directive
                  | block_stmt

stream_decl     ::= "~" identifier ("::" type)? ("=" expression)?
                  | "~" identifier ("," identifier)* ("::" type)? "=" expression ("," expression)*

route_stmt      ::= expression ("->" expression)+

function_decl   ::= annotation* "@fn" identifier "[" param_list? "]" ("=>" type)? "block" "[" statement* "]"

type_decl       ::= "@type" identifier "[" field_list "]"

annotation      ::= "@" identifier

param_list      ::= param ("," param)*
param           ::= "~" identifier "::" type

field_list      ::= (identifier "::" type newline)*

expression      ::= literal
                  | identifier
                  | "?" identifier
                  | "!" identifier
                  | "&" identifier
                  | "*" identifier
                  | expression "->" expression
                  | expression ">>" expression
                  | function_call
                  | block_expr
                  | fork_expr
                  | loop_expr
                  | "[" expression_list "]"

function_call   ::= identifier "(" arg_list? ")"

block_expr      ::= "block" ("[" param_list "]")? "[" statement* "]"

fork_expr       ::= "fork" "[" fork_arm+ "]"
fork_arm        ::= (probe_cond | "_") "->" expression

probe_cond      ::= "?" comparison_op literal
                  | "?" identifier comparison_op expression
                  | "@ok" | "@err"

loop_expr      ::= "loop" "[" expression "]" "[" statement* "]"
                  | "loop.range" "(" expression "," expression ")" "->" "[" "~" identifier "]" "[" statement* "]"

comparison_op   ::= "=" | "!=" | ">" | "<" | ">=" | "<="

type            ::= primitive_type
                  | "block" "[" integer "]"
                  | "slice" "[" type "]"
                  | "fn" "(" type "->" type ")"
                  | type "?"
                  | type "!" identifier
                  | "&" type
                  | identifier

primitive_type  ::= "int" | "int8" | "int16" | "int32" | "int64"
                  | "uint" | "uint8" | "uint16" | "uint32" | "uint64"
                  | "float" | "float32"
                  | "bool" | "byte" | "text" | "char" | "null"

literal         ::= integer | float | string | char | "true" | "false" | "null"

directive       ::= "@use" identifier ("[" identifier_list "]")?
                  | "@extern" function_decl
                  | "@static" stream_decl
                  | "@burst" identifier
                  | "@error" "(" string ")"
                  | "@bubble"
                  | "@break"
                  | "@skip"
```

---

## 18. Standard Library

The Dagger standard library (`@use std`) is itself written in Dagger. There is no privileged runtime — `std` is just a collection of `.dag` files.

### 18.1 std.io
I/O functions for files, stdin/stdout/stderr, formatting.

### 18.2 std.mem
Memory allocation, arena allocators, pool allocators.

```dagger
@use std.mem

~ arena = std.mem.arena(4096)               # create a 4KB arena
~ buf = arena -> std.mem.arena.alloc(128)   # allocate from arena
arena -> std.mem.arena.free                 # free entire arena at once
```

### 18.3 std.math
Full math function library including trig, log, random.

### 18.4 std.text
Text manipulation: parsing, formatting, searching, encoding.

### 18.5 std.collections

```dagger
@use std.collections

~ list :: std.collections.list[int]
42 -> list -> std.collections.list.push
list -> std.collections.list.len -> out.write
```

Available collections: `list`, `map`, `set`, `queue`, `stack`, `ring`.

### 18.6 std.sys
Direct system call functions. Platform-specific. Linux, macOS, Windows backends.

```dagger
@use std.sys

std.sys.exit(0)
```

### 18.7 std.fmt
Formatting values to text.

```dagger
@use std.fmt

42, "the answer is {}" -> std.fmt.format -> out.write
```

---

## 19. Compiler Pipeline

The Dagger compiler (`dagc`) transforms `.dag` source to a native binary in five stages.

### Stage 1: Lex
Source text → token stream. Handles comments, literals, operators, identifiers.

### Stage 2: Parse
Token stream → AST. Validates grammar per section 17. Produces a tree of routing nodes, block nodes, and function nodes.

### Stage 3: Type Resolution
AST → type-annotated AST. Infers types for all values and function outputs. Reports type mismatches. Resolves overloading if present.

### Stage 4: Value Lowering
Type-annotated AST → lower-level IR. This IR is a flat, SSA-like representation where:
- All values have explicit lifetimes
- All block boundaries are explicit enter/exit ops
- All routing chains are flattened to binary ops
- @burst and lifetime-end points are inserted

### Stage 5: Register Allocation
SIR → Register-allocated IR. Graph coloring assigns streams to registers or stack slots. @pin annotations are enforced. Spill code inserted where needed.

### Stage 6: Code Generation
Register-allocated IR → x86-64 assembly text (AT&T or Intel syntax, configurable). Then assembled via NASM/GAS or internal assembler to object file, then linked.

### Compiler Flags

```
dagc [flags] <source files>

-o <file>         output binary name (default: a.out)
-S                emit assembly only, do not assemble
-O0/O1/O2/O3      optimization level (default O2)
--syntax intel    emit Intel syntax assembly (default AT&T)
--no-stdlib       do not link standard library
--emit-sir        dump Stream IR before register allocation
--verbose         print stage timings
--check           type check only, no output
```

---

## 20. Examples

### 20.1 FizzBuzz

```dagger
# fizzbuzz.dag

~ i :: int = 1

loop [?i <= 100] [
    i -> fork [
        ?= 0 (i -> mod(15))  -> "FizzBuzz" -> out.writeln
        ?= 0 (i -> mod(3))   -> "Fizz"     -> out.writeln
        ?= 0 (i -> mod(5))   -> "Buzz"     -> out.writeln
        _                    -> i -> text.from -> out.writeln
    ]
    i -> add(1) -> i
]
```

### 20.2 Fibonacci

```dagger
# fib.dag

@fn fib [~ n :: int] => int [
    n -> fork [
        ?<= 1  -> n
        _      -> n -> sub(1) -> fib -> add(n -> sub(2) -> fib)
    ]
]

loop.range(0, 10) -> [~ i] [
    i -> fib -> out.writeln
]
```

### 20.3 Read a File and Print Line Count

```dagger
# linecount.dag
@use std.io

~ path :: text = in.read -> text.trim

path -> file.open -> fork [
    @err -> [~ e] [ "error: " -> text.join(e) -> out.write_err ]
    @ok  -> [~ f] [
        f -> file.read -> @bubble
          -> text.split("\n")
          -> slice.len
          -> text.from
          -> out.writeln
    ]
]
```

### 20.4 Struct and Gate Composition

```dagger
# vec2.dag

@type Vec2 [
    x :: float
    y :: float
]

@fn vec2.add [~ a :: Vec2, ~ b :: Vec2] => Vec2 [
    [ x = a.x -> add(b.x), y = a.y -> add(b.y) ]
]

@fn vec2.length [~ v :: Vec2] => float [
    v.x -> mul(v.x) -> add(v.y -> mul(v.y)) -> math.sqrt
]

~ a :: Vec2 = [ x = 3.0, y = 4.0 ]
~ b :: Vec2 = [ x = 1.0, y = 2.0 ]

a, b -> vec2.add -> vec2.length -> text.from -> out.writeln    # outputs 7.211...
```

### 20.5 Manual Memory and System Call

```dagger
# raw.dag
@use std.sys

~ buf :: block[64] @heap
"hello from heap\n" -> buf[0]

buf[0], 16 -> sys.write(1)    # fd=1 (stdout), buf, len

@burst buf
std.sys.exit(0)
```

---

## Appendix A: Keyword Reference

| Keyword | Meaning |
|---------|---------|
| `~` | Declare a stream |
| `->` | Route data |
| `=>` | Declare function output type |
| `::` | Bind type to value |
| `?` | Probe (non-consuming read) |
| `!` | Burst (consuming read + free) |
| `_` | Wildcard / discard |
| `@fn` | Declare a function |
| `@type` | Declare a type |
| `@burst` | Explicitly free a stream |
| `@static` | Static lifetime stream |
| `@pin` | Register pin annotation |
| `@inline` | Inline a function at call sites |
| `@extern` | External C ABI function |
| `@comptime` | Compile-time function |
| `@use` | Import a module |
| `@private` | Hide from module exports |
| `@error` | Produce an error value |
| `@bubble` | Propagate error upward |
| `@ok` / `@err` | Fork arms for error types |
| `@break` | Exit loop loop |
| `@skip` | Advance to next iteration |
| `@syscall` | Direct syscall function |
| `loop` | Loop construct |
| `fork` | Branch construct |
| `block` | Named scope block |
| `each` | Iterate a slice |
| `tee` | Probe + side-route + pass-through |
| `null` | Absence of value |
| `true` / `false` | Boolean literals |

---

## Appendix B: Error Messages

The Dagger compiler aims for clear, actionable error messages. Format:

```
[file]:[line]:[col] error: <message>
  <source line>
  <caret pointing to problem>
  hint: <suggestion>
```

Example:
```
main.dag:12:5 error: stream 'x' has been burst and cannot be probed
  x -> out.write
  ^
  hint: remove the earlier @burst on line 8, or use a copy before bursting
```

---

## Appendix C: Reserved Future Syntax

The following operators and keywords are reserved for future versions and may not be used as identifiers:

```
async await spawn chan select
match where impl trait
gpu kernel
```

---

*Dagger language specification — version 0.1.0-draft*  
*This document is intended as a seed spec for compiler implementation.*  
*All syntax is subject to revision during implementation.*
