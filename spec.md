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
5. [Streams](#5-streams)
6. [Shapes (Type System)](#6-shapes-type-system)
7. [Gates (Functions)](#7-gates-functions)
8. [Routing Operator](#8-routing-operator)
9. [Control Flow](#9-control-flow)
10. [Memory Model](#10-memory-model)
11. [Fields (Scope)](#11-fields-scope)
12. [Built-in Gates](#12-built-in-gates)
13. [Error Handling](#13-error-handling)
14. [Modules](#14-modules)
15. [Compile-time Gates (Macros)](#15-compile-time-gates-macros)
16. [Assembly Mapping](#16-assembly-mapping)
17. [Grammar (EBNF)](#17-grammar-ebnf)
18. [Standard Library](#18-standard-library)
19. [Compiler Pipeline](#19-compiler-pipeline)
20. [Examples](#20-examples)

---

## 1. Overview

Dagger is a systems programming language that compiles directly to x86-64 assembly with no runtime, no garbage collector, and no hidden cost. It is designed to make the flow of data through a program explicit, readable, and honest about what the hardware is actually doing.

Where most languages model programs as collections of objects or sequences of instructions, Dagger models programs as **networks of streams routed through gates**. Data flows. Gates transform it. Fields contain it. This is not a metaphor — it maps directly to how registers, ALU operations, and the call stack work at the silicon level.

Dagger is not a high-level language with an assembly backend. It is an assembly language with a humane syntax.

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

In Dagger, data always moves in one direction: left to right through the `->` operator. You never call a function — you route a stream through a gate. This isn't just aesthetic. It forces the programmer to think about what data is entering a transform and what shape it has when it exits.

```dagger
~ score :: int = 72
score -> validate -> rank -> out.write
```

This reads exactly like what the CPU does: load score, pass it to validate, pass the result to rank, write the final output.

### 2.2 Streams, Not Variables

Classical variables are locations. Dagger streams are flows. A stream has a shape (its data contract), a lifetime (when it exists), and a consumption model (probe vs burst). This distinction matters because reading a value and consuming it are different assembly operations — Dagger makes you say which you mean.

### 2.3 Spatial Scope

Scope in Dagger is physical. `[ ]` brackets define a field — a spatial region where streams exist. When execution leaves a field, everything declared inside it is freed. This maps directly to stack frame entry and exit. There is no garbage collector because there is no garbage — everything has a declared home.

### 2.4 Shape Inference Over Type Declarations

Dagger does not have a type system in the traditional sense. It has **shapes** — behavioral contracts that describe what a stream looks like and how it can be routed. Shapes are inferred from usage wherever possible. You annotate when you want to be explicit or when the compiler cannot infer.

### 2.5 Explicit Is Better Than Magic

If memory is allocated, you can see it. If a copy happens, you caused it. If something is freed, you wrote `@burst` or exited a field. There are no implicit operations in Dagger. Every assembly instruction has a visible reason in the source.

---

## 3. Lexical Structure

### 3.1 Comments

```dagger
# this is a single-line comment
```

There are no multi-line comments. Long explanations belong in documentation, not source.

### 3.2 Whitespace

Whitespace is not significant for parsing (Dagger is not indentation-sensitive) but convention is two-space indentation inside fields. Newlines separate statements. Multiple statements on one line are separated by `;`.

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
| `=>` | Output shape declaration (gate signatures only) |
| `::` | Shape bind |
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
~ @gate @burst @pin @static @extern @inline @comptime
loop fork field null true false
```

---

## 4. Core Concepts

### 4.1 The Three Primitives

Everything in Dagger is built from three primitives:

| Primitive | Symbol | What it is |
|-----------|--------|-----------|
| Stream | `~` | A named, shaped flow of data |
| Gate | `@gate` | A named transform that accepts and emits streams |
| Field | `[ ]` | A spatial scope that owns streams |

### 4.2 The Routing Operator

`->` is the single most important operator in Dagger. It routes the output of the left-hand side into the input of the right-hand side. It chains. It composes. It is the spine of every program.

```dagger
source -> transform -> sink
```

### 4.3 Execution Model

A `.dag` file's top-level statements form the entry stream. There is no `main()` function. The file is the program. Execution starts at the first statement and follows routing operators forward.

```dagger
# this is a complete, valid Dagger program
"hello, world" -> out.write
```

---

## 5. Streams

### 5.1 Declaration

```dagger
~ name :: shape = value
~ name :: shape          # uninitialized (must be written before probed)
~ name = value           # shape inferred
```

### 5.2 Probe vs Burst

```dagger
~ x :: int = 10

?x              # probe: reads x, x still exists, compiles to: mov rax, [x]
!x              # burst: reads x, x is freed, compiles to: mov rax, [x] ; (stack: adjust sp)
```

Probing a stream does not advance or consume it. Bursting a stream consumes it — after a burst, the stream is gone and any subsequent access is a compile error.

### 5.3 Stream Assignment (Re-routing)

```dagger
~ x :: int = 5
x -> add(1) -> x    # route x through add gate, result back into x
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

## 6. Shapes (Type System)

Shapes describe the contract of a stream — what it looks like in memory and what gates it can be routed into. Shapes are inferred wherever possible.

### 6.1 Primitive Shapes

| Shape | Size | Description |
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

### 6.2 Composite Shapes

#### Block (fixed-size buffer)
```dagger
~ buf :: block[256]        # 256 bytes on stack
~ buf :: block[256] @heap  # 256 bytes on heap
```

#### Slice (pointer + length)
```dagger
~ items :: slice[int]      # dynamic-length sequence of ints
```

#### Struct shape
```dagger
@shape Point [
    x :: float
    y :: float
]

~ p :: Point = [ x = 1.0, y = 2.0 ]
p.x -> out.write
```

#### Union shape
```dagger
@shape Num [
    | int
    | float
]
```

#### Gate shape (first-class gates)
```dagger
~ transform :: gate(int -> int)
```

#### Optional shape
```dagger
~ maybe :: int?            # int or null
```

#### Error shape
```dagger
~ result :: int!err        # int or an error — see section 13
```

### 6.3 Shape Inference

The compiler infers shapes from:
- Literal values (`42` infers `int`, `3.14` infers `float`, `"x"` infers `text`)
- Gate output shapes
- Routing chain context

When inference is ambiguous, the compiler emits a descriptive error asking for an explicit annotation.

### 6.4 Shape Casting

```dagger
~ x :: int = 65
x -> cast(char) -> out.write    # routes x through cast gate, output shape is char
```

Casts are explicit gate calls, never implicit. Narrowing casts that could lose data require `cast.unsafe`.

---

## 7. Gates

Gates are the transformation units of Dagger. A gate accepts one or more input streams, transforms them, and emits an output stream. Gates do not "execute" — data is *routed through* them.

### 7.1 Gate Declaration

```dagger
@gate name [inputs] => output_shape [
    # body: last expression is the output
]
```

```dagger
@gate double [~ n :: int] => int [
    n -> mul(2)
]
```

The body of a gate is a field. The last routed value in the field is the gate's output. No `return` keyword.

### 7.2 Multiple Inputs

```dagger
@gate add_scaled [~ a :: int, ~ b :: int, ~ factor :: int] => int [
    b -> mul(factor) -> add(a)
]

3, 4, 2 -> add_scaled -> out.write    # outputs 11
```

### 7.3 Multiple Outputs

```dagger
@gate minmax [~ a :: int, ~ b :: int] => [int, int] [
    a -> min(b),
    a -> max(b)
]

~ lo, hi = 5, 9 -> minmax
```

### 7.4 Recursive Gates

```dagger
@gate factorial [~ n :: int] => int [
    n -> fork [
        ?<= 1  -> 1
        _      -> n -> sub(1) -> factorial -> mul(n)
    ]
]
```

### 7.5 Inline Gates

```dagger
@inline @gate square [~ n :: int] => int [
    n -> mul(n)
]
```

`@inline` instructs the compiler to inline the gate body at every call site. No `call` instruction is emitted.

### 7.6 First-class Gates

Gates are streams. They can be stored, passed, and routed.

```dagger
~ op :: gate(int -> int) = double

5 -> op -> out.write    # outputs 10
```

### 7.7 Gate Composition

```dagger
~ pipeline = double >> square    # compose: double then square
5 -> pipeline -> out.write       # outputs 100
```

`>>` composes two gates into one. The output shape of the left gate must match the input shape of the right gate.

### 7.8 Partial Application

```dagger
~ add5 = add(5)    # partially apply add with second arg = 5
3 -> add5          # outputs 8
```

---

## 8. Routing Operator

### 8.1 Basic Routing

```dagger
source -> gate
```

### 8.2 Chained Routing

```dagger
source -> gate_a -> gate_b -> gate_c
```

Left-associative. Each gate's output becomes the next gate's input.

### 8.3 Routing Into a Stream

```dagger
source -> gate -> ~ result
# or into an existing stream:
source -> gate -> result
```

### 8.4 Routing Into a Field

```dagger
source -> [
    # source is available here as the field's input stream
    # last expression exits the field as output
]
```

### 8.5 Routing Multiple Streams

```dagger
a, b -> gate_that_takes_two
```

### 8.6 Discarding Output

```dagger
source -> gate -> _    # route to wildcard: output discarded
```

### 8.7 Tee Routing (split without consuming)

```dagger
source -> tee(log.write) -> next_gate
```

`tee` probes the stream, routes a copy to its argument gate, and passes the original forward unchanged.

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

Fork with output (all arms must emit the same shape):
```dagger
~ label = x -> fork [
    ?>= 90  -> "excellent"
    ?>= 60  -> "passing"
    _       -> "fail"
]
```

### 9.2 Loop

`loop` repeats its body field while its condition field evaluates to `true`.

```dagger
loop [condition] [
    # body
]
```

```dagger
~ i :: int = 0

loop [?i < 10] [
    i -> out.write
    i -> add(1) -> i
]
```

The condition field is probed (non-consuming) on each iteration. The body field is a full field — streams declared inside are freed on each iteration.

Loop with index:
```dagger
loop.range(0, 10) -> [~ i] [
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

Inside a `loop` or `each` field:

```dagger
loop [?i < 100] [
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

All streams declared in a field live on the stack by default. They are freed when the field exits.

```dagger
~ x :: int = 42           # stack allocated, 8 bytes
~ buf :: block[128]       # stack allocated, 128 bytes
```

The compiler tracks stack frame size at compile time and emits a single `sub rsp, N` on field entry.

### 10.2 Heap Allocation

```dagger
~ buf :: block[1024] @heap    # heap allocated
~ arr :: slice[int] @heap     # heap-allocated slice
```

Heap streams must be explicitly freed with `@burst` or they will be freed on scope exit if inside a `field` block. The compiler warns on unfreed heap streams at the end of their declaring scope.

### 10.3 Explicit Free

```dagger
@burst buf     # frees buf, compiles to call free / or direct dealloc
```

After `@burst`, the stream is gone. Any use after burst is a compile error.

### 10.4 Stack vs Heap Decision Guide

| Use stack when | Use heap when |
|----------------|---------------|
| Size is known at compile time | Size is determined at runtime |
| Lifetime matches the current field | Lifetime outlasts the current field |
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
~ ref = &x           # ref holds the address of x, shape: &int

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

## 11. Fields (Scope)

### 11.1 Basic Field

```dagger
field [
    ~ x :: int = 5
    x -> out.write
]
# x is freed here
```

### 11.2 Fields as Expressions

A field can produce an output value — the last routed expression in the field is its result:

```dagger
~ result = field [
    ~ a = 3
    ~ b = 4
    a -> add(b)    # this is the field's output
]
result -> out.write    # outputs 7
```

### 11.3 Named Fields

Fields can be named for clarity (name has no semantic effect, only documentary):

```dagger
field.setup [
    # initialization
]

field.main [
    # program body
]
```

### 11.4 Field Inputs

```dagger
~ x = 10
x -> field [~ n] [
    n -> mul(2) -> out.write
]
```

The routed value enters the field as the named stream `n`.

---

## 12. Built-in Gates

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
| `cast(shape)` | safe shape cast |
| `cast.unsafe(shape)` | unsafe reinterpret cast |
| `tee(gate)` | probe + side-route, pass through |
| `tap(gate)` | alias for tee |
| `id` | identity gate — passes input through unchanged |
| `const(v)` | discards input, emits constant v |
| `assert(cond)` | halts with diagnostic if condition fails |
| `unreachable` | marks a path as impossible; if reached, halts |

---

## 13. Error Handling

Dagger has no exceptions. Errors are shapes — a gate that can fail returns an error shape, and the caller must route it explicitly.

### 13.1 Error Shape

```dagger
~ result :: int!err    # either an int or an error
```

The `!err` suffix marks a stream as potentially an error.

### 13.2 Producing Errors

```dagger
@gate divide [~ a :: int, ~ b :: int] => int!err [
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
@gate safe_sqrt [~ n :: float] => float!err [
    n -> fork [
        ?< 0.0  -> @error("negative input")
        _       -> n -> math.sqrt
    ]
]

@gate compute [~ x :: float] => float!err [
    x -> safe_sqrt -> @bubble    # @bubble propagates error upward if present
    # if safe_sqrt succeeds, routing continues here
]
```

`@bubble` short-circuits: if the stream is an error, it exits the current gate with that error. If it is `@ok`, routing continues with the unwrapped value.

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

`@use` makes the module's exported gates and streams available under its name.

### 14.3 Selective Import

```dagger
@use math [ double, square ]

5 -> double -> out.write
```

### 14.4 Exporting

By default, all top-level gates and `@static` streams are exported. Prefix with `@private` to hide from importers:

```dagger
@private @gate helper [~ n :: int] => int [
    n -> mul(3)
]

@gate triple [~ n :: int] => int [
    n -> helper
]
```

### 14.5 External (C ABI)

```dagger
@extern @gate printf [~ fmt :: &byte, ...] => int
@extern @gate malloc [~ size :: uint] => &byte
```

`@extern` declares a gate with C calling convention. Dagger will emit the correct `call` instruction and handle ABI-compliant argument passing.

---

## 15. Compile-time Gates (Macros)

`@comptime` gates run entirely at compile time. Their output is substituted inline as a constant. They have access to shape information and literal values only — no runtime state.

```dagger
@comptime @gate kilobytes [~ n :: int] => int [
    n -> mul(1024)
]

~ buf :: block[@kilobytes(4)]    # compiles to block[4096]
```

Comptime gates are used for:
- Constant folding
- Computed buffer sizes
- Conditional compilation
- Code generation over shapes

```dagger
@comptime @gate is_64bit => bool [
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

### 16.3 Gate Call

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

### 16.5 loop (Loop)

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

### 16.6 Field Entry/Exit

```dagger
field [
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

Syscall gates emit direct `syscall` instructions with the correct register setup per the Linux x86-64 ABI (rax=syscall number, rdi, rsi, rdx, r10, r8, r9 for args).

---

## 17. Grammar (EBNF)

```ebnf
program         ::= statement*

statement       ::= stream_decl
                  | route_stmt
                  | gate_decl
                  | shape_decl
                  | directive
                  | field_stmt

stream_decl     ::= "~" identifier ("::" shape)? ("=" expression)?
                  | "~" identifier ("," identifier)* ("::" shape)? "=" expression ("," expression)*

route_stmt      ::= expression ("->" expression)+

gate_decl       ::= annotation* "@gate" identifier "[" param_list? "]" ("=>" shape)? "[" statement* "]"

shape_decl      ::= "@shape" identifier "[" field_list "]"

annotation      ::= "@" identifier

param_list      ::= param ("," param)*
param           ::= "~" identifier "::" shape

field_list      ::= (identifier "::" shape newline)*

expression      ::= literal
                  | identifier
                  | "?" identifier
                  | "!" identifier
                  | "&" identifier
                  | "*" identifier
                  | expression "->" expression
                  | expression ">>" expression
                  | gate_call
                  | field_expr
                  | fork_expr
                  | loop_expr
                  | "[" expression_list "]"

gate_call       ::= identifier "(" arg_list? ")"

field_expr      ::= "field" ("[" param_list "]")? "[" statement* "]"

fork_expr       ::= "fork" "[" fork_arm+ "]"
fork_arm        ::= (probe_cond | "_") "->" expression

probe_cond      ::= "?" comparison_op literal
                  | "?" identifier comparison_op expression
                  | "@ok" | "@err"

loop_expr      ::= "loop" "[" expression "]" "[" statement* "]"
                  | "loop.range" "(" expression "," expression ")" "->" "[" "~" identifier "]" "[" statement* "]"

comparison_op   ::= "=" | "!=" | ">" | "<" | ">=" | "<="

shape           ::= primitive_shape
                  | "block" "[" integer "]"
                  | "slice" "[" shape "]"
                  | "gate" "(" shape "->" shape ")"
                  | shape "?"
                  | shape "!" identifier
                  | "&" shape
                  | identifier

primitive_shape ::= "int" | "int8" | "int16" | "int32" | "int64"
                  | "uint" | "uint8" | "uint16" | "uint32" | "uint64"
                  | "float" | "float32"
                  | "bool" | "byte" | "text" | "char" | "null"

literal         ::= integer | float | string | char | "true" | "false" | "null"

directive       ::= "@use" identifier ("[" identifier_list "]")?
                  | "@extern" gate_decl
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
I/O gates for files, stdin/stdout/stderr, formatting.

### 18.2 std.mem
Memory allocation, arena allocators, pool allocators.

```dagger
@use std.mem

~ arena = std.mem.arena(4096)               # create a 4KB arena
~ buf = arena -> std.mem.arena.alloc(128)   # allocate from arena
arena -> std.mem.arena.free                 # free entire arena at once
```

### 18.3 std.math
Full math gate library including trig, log, random.

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
Direct system call gates. Platform-specific. Linux, macOS, Windows backends.

```dagger
@use std.sys

std.sys.exit(0)
```

### 18.7 std.fmt
Formatting streams to text.

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
Token stream → AST (Abstract Stream Tree). Validates grammar per section 17. Produces a tree of routing nodes, field nodes, gate nodes.

### Stage 3: Shape Resolution
AST → shape-annotated AST. Infers shapes for all streams and gate outputs. Reports shape mismatches. Resolves gate overloading if present.

### Stage 4: Stream Lowering
Shape-annotated AST → Stream IR (SIR). SIR is a flat, SSA-like representation where:
- All streams have explicit lifetimes
- All field boundaries are explicit enter/exit ops
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
--check           type/shape check only, no output
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

@gate fib [~ n :: int] => int [
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

@shape Vec2 [
    x :: float
    y :: float
]

@gate vec2.add [~ a :: Vec2, ~ b :: Vec2] => Vec2 [
    [ x = a.x -> add(b.x), y = a.y -> add(b.y) ]
]

@gate vec2.length [~ v :: Vec2] => float [
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
| `=>` | Declare gate output shape |
| `::` | Bind shape to stream |
| `?` | Probe (non-consuming read) |
| `!` | Burst (consuming read + free) |
| `_` | Wildcard / discard |
| `@gate` | Declare a gate |
| `@shape` | Declare a shape |
| `@burst` | Explicitly free a stream |
| `@static` | Static lifetime stream |
| `@pin` | Register pin annotation |
| `@inline` | Inline a gate at call sites |
| `@extern` | External C ABI gate |
| `@comptime` | Compile-time gate |
| `@use` | Import a module |
| `@private` | Hide from module exports |
| `@error` | Produce an error value |
| `@bubble` | Propagate error upward |
| `@ok` / `@err` | Fork arms for error shapes |
| `@break` | Exit loop loop |
| `@skip` | Advance to next iteration |
| `@syscall` | Direct syscall gate |
| `loop` | Loop gate |
| `fork` | Branch gate |
| `field` | Named scope block |
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
