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
5. [Streams (Variables)](#5-streams-variables)
6. [Types](#6-types)
7. [Functions](#7-functions)
8. [Routing Operator](#8-routing-operator)
9. [Control Flow](#9-control-flow)
10. [Memory Model](#10-memory-model)
11. [Blocks (Scope)](#11-blocks-scope)
12. [Built-in Functions](#12-built-in-functions)
13. [Error Handling](#13-error-handling)
14. [Modules](#14-modules)
15. [Compile-time Functions](#15-compile-time-functions)
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

### Goals

- Every line of Dagger source corresponds to a predictable, auditable sequence of assembly instructions
- No hidden allocations, no implicit copies, no surprise destructor calls
- Readable left-to-right data flow with the `->` routing operator
- Type inference that requires zero annotations for straightforward code
- Explicit memory ownership without a borrow checker — the programmer is trusted
- Compile to a single flat binary with no dependencies

### Non-goals

- Garbage collection
- A standard runtime or VM
- Object-oriented dispatch (no vtables, no inheritance)
- Exceptions (Dagger uses typed error values instead)
- Cross-platform abstraction (target is x86-64; other architectures are future work)

---

## 2. Design Philosophy

### 2.1 Data Has Direction

In Dagger, data always moves in one direction: left to right through the `->` operator. You do not hide control flow behind implicit dispatch; you route a value through an explicit function. This forces the programmer to think about what data is entering a transform and what type it has when it exits.

```dagger
~ score :: int = 72
score -> validate -> rank -> out.write
```

This reads exactly like what the CPU does: load score, pass it to validate, pass the result to rank, write the final output.

### 2.2 Probe vs Burst

Reading a value and consuming it are different assembly operations — Dagger makes you say which you mean. `?x` probes `x` (non-consuming read). `!x` bursts `x` (reads and frees it). This distinction is explicit in every use of a stream.

### 2.3 Explicit Scope

Scope in Dagger is physical. `[ ]` brackets define a block — a region where streams are declared and owned. When execution leaves a block, everything declared inside it is freed. This maps directly to stack frame entry and exit. There is no garbage collector because there is no garbage — everything has a declared home.

### 2.4 Type Inference

Dagger does not require explicit type annotations everywhere. Types are inferred from literals, function output types, and routing chain context. You annotate when you want to be explicit or when the compiler cannot infer.

### 2.5 No Hidden Costs

If memory is allocated, you can see it. If a copy happens, you caused it. If something is freed, you wrote `@burst` or exited a block. There are no implicit operations in Dagger. Every assembly instruction has a visible reason in the source.

---

## 3. Lexical Structure

### 3.1 Comments

```dagger
# this is a single-line comment
```

There are no multi-line comments.

### 3.2 Whitespace and Statements

Whitespace is not significant for parsing (Dagger is not indentation-sensitive). Convention is two-space indentation inside blocks. Newlines separate statements. Multiple statements on one line are separated by `;`.

### 3.3 Identifiers

Identifiers begin with a letter or underscore, followed by letters, digits, underscores, or dots. Dots namespace identifiers (e.g. `out.write`, `mem.alloc`).

```
identifier ::= [a-zA-Z_][a-zA-Z0-9_.]*
```

**Name collision rule:** if a user module shares a name with a built-in namespace (e.g. a module named `text`), the module name takes precedence and the built-in is accessed via its full qualified path. The compiler emits a warning when this occurs.

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

# null (absence of value — see section 6.5)
null
```

### 3.5 Operators

| Operator | Meaning |
|----------|---------|
| `->` | Route (data flows right) |
| `=>` | Function return type annotation |
| `::` | Type annotation |
| `=` | Stream initialisation |
| `?` | Probe (non-consuming read) |
| `!` | Burst (consuming read + free) |
| `>>` | Function composition |
| `@` | Directive prefix |
| `_` | Wildcard / discard |
| `..` | Range |
| `[n]` | Index access |
| `,` | Multi-stream separator |
| `&` | Address-of |
| `*` | Dereference |

**Note:** the probe comparisons `?>`, `?<`, `?>=`, `?<=`, `?=`, `?!=` used inside `fork` arms are **not** standalone operators. They are part of the `fork` arm syntax and are only valid there. See Section 9.1.

### 3.6 Keywords

```
~ @fn @type @burst @static @extern @inline @comptime @private @use
@syscall @error @bubble @or @ok @err @break @skip
loop fork block each tee null true false
```

---

## 4. Core Concepts

### 4.1 The Three Primitives

Everything in Dagger is built from three primitives:

| Primitive | Syntax | What it is |
|-----------|--------|------------|
| Stream | `~ name` | A named, typed value with an explicit lifetime |
| Function | `@fn name` | A named transform: takes inputs, emits output |
| Block | `[ ]` | A scope that owns all streams declared inside it |

### 4.2 The Routing Operator

`->` is the central operator in Dagger. It routes the output of the left-hand side into the input of the right-hand side.

```dagger
source -> transform -> sink
```

### 4.3 Execution Model

A `.dag` file's top-level statements form the program entry. There is no `main()` function. Execution starts at the first statement and proceeds top to bottom, following routing operators.

```dagger
# this is a complete, valid Dagger program
"hello, world" -> out.write
```

---

## 5. Streams (Variables)

A **stream** is a named, typed value with an explicit lifetime. Streams are declared with `~`.

### 5.1 Declaration

```dagger
~ name :: type = value    # explicit type, initialised
~ name :: type            # explicit type, uninitialised (must be written before probed)
~ name = value            # type inferred from value
```

### 5.2 Probe vs Burst

There are two ways to read a stream:

```dagger
~ x :: int = 10

?x    # PROBE: reads x without consuming it. x still exists after this.
      # compiles to: mov rax, [x]

!x    # BURST: reads x and frees it. x no longer exists after this.
      # compiles to: mov rax, [x]  (then x's stack slot is reclaimed)
```

After a burst, any further use of `x` is a **compile error**. The compiler tracks burst state statically.

Probing through a reference does not free the referenced value. See Section 10.6.

### 5.3 Reassignment by Routing

```dagger
~ x :: int = 5
x -> add(1) -> x    # route x through add(1), result stored back into x
```

Routing a value back into the same stream is an in-place update. The compiler emits a single register operation.

### 5.4 Multi-stream Declaration

```dagger
~ a, b, c :: int = 1, 2, 3
```

All streams on the left share the same type. The right-hand side must supply exactly as many values.

### 5.5 Register Pinning

```dagger
~ counter :: int @rax    # compiler must keep counter in rax
```

Pins a stream to a specific x86-64 register. The compiler emits an error if the register is unavailable at the pin point. Useful for syscalls and ABI-sensitive code.

### 5.6 Static Streams

```dagger
@static ~ pi :: float = 3.14159
```

Stored in `.data` or `.rodata`. Exists for the lifetime of the program. Cannot be burst.

---

## 6. Types

### 6.1 Primitive Types

| Type | Size | Description |
|------|------|-------------|
| `int` | 64-bit | Signed integer |
| `int8` `int16` `int32` `int64` | explicit | Sized signed integers |
| `uint` `uint8` `uint16` `uint32` `uint64` | explicit | Unsigned integers |
| `float` | 64-bit | IEEE 754 double |
| `float32` | 32-bit | IEEE 754 single |
| `bool` | 1 byte | `true` or `false` |
| `byte` | 8-bit | Raw byte |
| `text` | ptr+len | UTF-8 string (fat pointer: address + byte length, no null terminator) |
| `char` | 32-bit | Unicode code point (UTF-32) |

### 6.2 Composite Types

#### Fixed-size buffer (`block[N]`)

```dagger
~ buf :: block[256]        # 256 bytes, stack allocated
~ buf :: block[256] @heap  # 256 bytes, heap allocated
```

`block[N]` is a raw byte buffer of exactly N bytes. N must be a compile-time constant.

#### Slice (`slice[T]`)

```dagger
~ items :: slice[int]    # fat pointer: (address, element count)
```

A slice does not own its memory. It is a view into a `block` or heap buffer. See Section 10.7.

#### Struct (`@type`)

```dagger
@type Point [
    x :: float
    y :: float
]

~ p :: Point = [ x = 1.0, y = 2.0 ]
p.x -> out.write
```

#### Union (`@type` with variants)

A union type holds exactly one of its listed variants at a time.

```dagger
@type Num [
    | int
    | float
]
```

**Constructing a union value:**

```dagger
~ n :: Num = int(42)      # construct the int variant
~ m :: Num = float(3.14)  # construct the float variant
```

**Matching on a union value:**

```dagger
n -> fork [
    int   -> [~ v :: int]   [ v -> out.write ]
    float -> [~ v :: float] [ v -> out.write ]
]
```

Union arms in `fork` match on the variant name, not a probe comparison. The matched value is bound in the block input.

#### Function type

```dagger
~ transform :: fn(int -> int)
```

Stores a reference to any function that takes one `int` and returns one `int`. See Section 7.6.

#### Optional type (`T?`)

```dagger
~ maybe :: int?    # holds either an int or null
```

`null` is the only valid "no value" representation. The `?` suffix is the only way to declare a stream that may be `null` — non-optional streams can never hold `null`.

#### Error type (`T!err`)

```dagger
~ result :: int!err    # holds either an int or an error value
```

See Section 13.

### 6.3 The `null` Value

`null` represents the absence of a value. Its type is `null`. It can be assigned only to optional streams (`T?`) or used as the return type of functions that produce no useful output.

```dagger
~ x :: int? = null    # valid: x is optional
~ y :: int  = null    # COMPILE ERROR: int is not optional
```

### 6.4 Type Inference Rules

The compiler infers types using the following rules, applied in order:

1. **Literal:** `42` → `int`, `3.14` → `float`, `"x"` → `text`, `true`/`false` → `bool`, `null` → `null`
2. **Function output:** the stream receives the declared output type of the function it was routed from
3. **Routing context:** if the destination function has a known input type, that type propagates back

If inference is ambiguous after these rules, the compiler emits an error requesting an explicit `::` annotation.

### 6.5 Type Casting

```dagger
~ x :: int = 65
x -> cast(char) -> out.write    # output type is char
```

Casts are explicit function calls. Widening casts (e.g. `int32` to `int64`) use `cast`. Narrowing casts that may lose data require `cast.unsafe`.

---

## 7. Functions

A function takes one or more input streams, transforms them, and emits an output value. Data is *routed through* a function — functions do not execute in isolation.

### 7.1 Declaration

```dagger
@fn name [~ param :: type, ...] => return_type [
    # body
    # the last expression in the body is the return value
    # no return keyword
]
```

```dagger
@fn double [~ n :: int] => int [
    n -> mul(2)
]
```

### 7.2 Multiple Inputs

Inputs are declared as a comma-separated parameter list. At the call site, values are supplied left to right in the same order.

```dagger
@fn add_scaled [~ a :: int, ~ b :: int, ~ factor :: int] => int [
    b -> mul(factor) -> add(a)
]

3, 4, 2 -> add_scaled -> out.write    # a=3, b=4, factor=2, outputs 11
```

### 7.3 Multiple Outputs

```dagger
@fn minmax [~ a :: int, ~ b :: int] => [int, int] [
    a -> min(b),
    a -> max(b)
]

~ lo, hi = 5, 9 -> minmax
```

The output list is comma-separated. The receiving declaration must have exactly as many names as output values.

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

`@inline` causes the compiler to substitute the function body at every call site. No `call` instruction is emitted.

### 7.6 First-class Functions

A function can be stored in a stream and routed through like any other value. The function type `fn(A -> B)` describes a function taking one input of type `A` and returning type `B`.

```dagger
~ op :: fn(int -> int) = double

5 -> op -> out.write    # indirect call; compiles to: call [op]
```

**Multi-parameter function type:**

```dagger
~ combiner :: fn(int, int -> int) = add_scaled
```

A function value holds only a pointer to the function. It does not capture any surrounding state. If you need to carry state with a function, use partial application (Section 7.8).

### 7.7 Function Composition

`>>` composes two functions into a new function value. The output type of the left must match the input type of the right.

```dagger
~ pipeline :: fn(int -> int) = double >> square
5 -> pipeline -> out.write    # outputs 100
```

The composed function is created at compile time when both operands are known function names. The result is a plain function pointer — no hidden allocation.

### 7.8 Partial Application

Calling a multi-parameter built-in with fewer arguments than it expects produces a partially applied function value.

```dagger
~ add5 :: fn(int -> int) = add(5)    # fixes second argument of add to 5
3 -> add5                             # outputs 8
```

The captured argument is stored inline in a compiler-generated wrapper function in `.text`. No heap allocation occurs. Partial application is only valid for built-in functions and `@fn` declarations in the current compilation unit — it is not valid for `@extern` or function-pointer values.

---

## 8. Routing Operator

### 8.1 Basic Routing

```dagger
source -> fn
```

Routes the value `source` into function `fn`. The result is the output of `fn`.

### 8.2 Chained Routing

```dagger
source -> fn_a -> fn_b -> fn_c
```

Left-associative. Equivalent to `fn_c(fn_b(fn_a(source)))`.

### 8.3 Routing Into a Stream

```dagger
source -> fn -> ~ result       # declare new stream, type inferred from fn's output
source -> fn -> result         # store result into existing stream
```

### 8.4 Routing Into a Block

```dagger
source -> block [~ n :: type] [
    # n holds the routed value
    # last expression is the block's output
]
```

The block input parameter `[~ n :: type]` is required when routing a value into a block. The type annotation is optional if it can be inferred. See Section 11.4.

### 8.5 Routing Multiple Streams

```dagger
a, b -> fn_that_takes_two
```

The comma-separated values are passed to the function's parameters left to right.

### 8.6 Discarding Output

```dagger
source -> fn -> _    # output discarded; compiles to nothing after the call
```

### 8.7 Tee Routing

```dagger
source -> tee(log.write) -> next_fn
```

`tee` probes the value (non-consuming), routes a copy to the argument function, and passes the original forward unchanged. Useful for logging and debugging without breaking a pipeline.

---

## 9. Control Flow

### 9.1 Fork (Branching)

`fork` routes the input value into the first arm whose condition matches. Arms are checked top to bottom. The first match wins; remaining arms are not evaluated.

**Syntax:**

```dagger
value -> fork [
    arm_condition -> routing_chain
    arm_condition -> routing_chain
    _             -> routing_chain    # catch-all; matches anything
]
```

An arm condition is one of:

| Condition form | Meaning |
|---|---|
| `?= literal` | input equals literal |
| `?!= literal` | input does not equal literal |
| `?> literal` | input greater than literal |
| `?< literal` | input less than literal |
| `?>= literal` | input greater than or equal to literal |
| `?<= literal` | input less than or equal to literal |
| `@ok` | input is a success value (for `T!err` types) |
| `@err` | input is an error value (for `T!err` types) |
| `TypeName` | input holds this variant (for union types) |
| `_` | always matches (catch-all) |

The `?` prefix in conditions is part of the `fork` arm syntax. It does not mean "probe" in this context — it means "test the routed value against this condition." Probe (`?x`) and burst (`!x`) as used in Section 5.2 are stream-prefix operators, not fork conditions.

If no arm matches and there is no `_` catch-all, the compiler emits a warning.

**Fork with no output** (all arms are sinks):

```dagger
x -> fork [
    ?> 100  -> handle_overflow
    _       -> process
]
```

**Fork with output** (all arms must emit the same type; the result can be stored):

```dagger
~ label :: text = x -> fork [
    ?>= 90  -> "excellent"
    ?>= 60  -> "passing"
    _       -> "fail"
]
```

**Fork on a union type** (see Section 6.2):

```dagger
n -> fork [
    int   -> [~ v :: int]   [ v -> out.write ]
    float -> [~ v :: float] [ v -> out.write ]
]
```

**Fork on an error type** (see Section 13):

```dagger
val -> fork [
    @ok  -> [~ n] [ n -> out.write ]
    @err -> [~ e] [ e -> out.write_err ]
]
```

### 9.2 Loop

`loop` repeats its body while its condition is true. Both the condition and the body are enclosed in `[ ]`. The `block` keyword is not used here.

```dagger
loop [condition_expression] [
    # body
]
```

```dagger
~ i :: int = 0

loop [i -> lt(10)] [
    i -> out.write
    i -> add(1) -> i
]
```

The condition is re-evaluated on each iteration. Values declared inside the body are freed at the end of each iteration.

**Loop with range:**

```dagger
loop.range(0, 10) -> [~ i] [
    i -> out.write
]
```

`loop.range(start, end)` produces an `int` stream that increments from `start` to `end - 1` (exclusive). The value is bound via the block input syntax `[~ i]`.

### 9.3 Each (Slice Iteration)

`each` iterates the elements of a slice one by one.

```dagger
~ nums :: slice[int] = [1, 2, 3, 4, 5]

nums -> each -> [~ n] [
    n -> mul(2) -> out.write
]
```

Each element is bound via the block input syntax `[~ n]`. The type of `n` is inferred as the element type of the slice.

### 9.4 Break and Skip

`@break` exits the nearest enclosing `loop` or `each` immediately.  
`@skip` ends the current iteration and advances to the next.

Both are valid only inside a `loop` or `each` body.

```dagger
loop [i -> lt(100)] [
    i -> fork [
        ?= 50  -> @break
        _      -> i -> out.write
    ]
    i -> add(1) -> i
]
```

```dagger
nums -> each -> [~ n] [
    n -> fork [
        ?< 0  -> @skip
        _     -> n -> out.write
    ]
]
```

---

## 10. Memory Model

Dagger has no garbage collector. All memory is stack-allocated, statically allocated, or explicitly heap-allocated.

### 10.1 Stack Allocation

All streams declared inside a block live on the stack by default. They are freed when the block exits.

```dagger
~ x :: int = 42        # 8 bytes on the stack
~ buf :: block[128]    # 128 bytes on the stack
```

The compiler computes the total frame size at compile time and emits a single `sub rsp, N` on block entry and `add rsp, N` on exit.

### 10.2 Heap Allocation

Adding `@heap` to a declaration allocates on the heap via the system allocator (`malloc` / `mmap` — the allocator is configurable via `std.mem`; see Section 18.2).

```dagger
~ buf :: block[1024] @heap    # 1024 bytes, heap allocated
~ arr :: slice[int]  @heap    # heap-allocated slice
```

The compiler emits a warning if a `@heap` stream reaches the end of its declaring scope without being freed.

### 10.3 Explicit Free

```dagger
@burst buf    # frees buf; compiles to: mov rdi, [buf] / call free
```

After `@burst`, `buf` is gone. Any subsequent use is a compile error. Freeing a stack-allocated stream with `@burst` is also valid — it marks the stream as dead before scope exit, and the compiler reclaims the stack slot if possible.

### 10.4 Stack vs Heap

| Use stack when | Use heap when |
|----------------|---------------|
| Size is known at compile time | Size is determined at runtime |
| Lifetime matches the current block | Lifetime outlasts the current block |
| Size is small (under a few KB) | Size is large |

### 10.5 Copies and Moves

```dagger
~ a :: int = 5
~ b = a       # COPY: a and b both exist. compiles to: mov rbx, rax

~ c = !a      # MOVE (burst): a is consumed, c gets the value, a is freed.
```

Copies of primitive types are always a single register move. Copies of `block` or `slice` values copy the entire buffer — use references to avoid this.

### 10.6 References

```dagger
~ x :: int = 10
~ ref :: &int = &x      # ref holds the address of x

*ref -> out.write       # dereference: reads value at ref's address
```

References have no lifetime tracking. The programmer is responsible for ensuring the referenced stream outlives the reference. Bursting the referenced stream while a live reference exists is not a compile error, but using the reference afterward is undefined behavior.

### 10.7 Slices

A slice is a `(pointer, element_count)` pair. It does not own its memory — it is a view into a `block` or heap buffer.

```dagger
~ buf  :: block[256]
~ view :: slice[byte] = buf[0..128]    # view of the first 128 bytes of buf
```

Slices become invalid if the underlying buffer is freed. The compiler does not track this — the programmer is responsible.

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

The last expression in a block is its output value. This can be captured into a stream.

```dagger
~ result = block [
    ~ a = 3
    ~ b = 4
    a -> add(b)    # output of this block
]
result -> out.write    # outputs 7
```

### 11.3 Named Blocks

Blocks can be given a documentary name with no semantic effect:

```dagger
block.setup [
    # initialization
]

block.main [
    # program body
]
```

### 11.4 Block Inputs

A block can receive a routed value as a named parameter:

```dagger
~ x = 10
x -> block [~ n :: int] [
    n -> mul(2) -> out.write
]
```

The type annotation `:: int` is optional when it can be inferred from the routed value. The parameter syntax `[~ n]` is only valid immediately before the body `[ ]` — it is not the same as a standalone `~ n` declaration.

---

## 12. Built-in Functions

### 12.1 Arithmetic

| Function | Input | Output | Notes |
|----------|-------|--------|-------|
| `add(n)` | num | num | adds n |
| `sub(n)` | num | num | subtracts n |
| `mul(n)` | num | num | multiplies by n |
| `div(n)` | num | num | divides by n |
| `mod(n)` | int | int | modulo |
| `neg` | num | num | negate |
| `abs` | num | num | absolute value |
| `min(n)` | num | num | minimum of input and n |
| `max(n)` | num | num | maximum of input and n |
| `pow(n)` | num | num | raises to power n |

### 12.2 Bitwise

| Function | Input | Output | Notes |
|----------|-------|--------|-------|
| `bit.and(n)` | int | int | bitwise AND |
| `bit.or(n)` | int | int | bitwise OR |
| `bit.xor(n)` | int | int | bitwise XOR |
| `bit.not` | int | int | bitwise NOT |
| `bit.shl(n)` | int | int | shift left by n bits |
| `bit.shr(n)` | int | int | logical shift right by n bits |
| `bit.sar(n)` | int | int | arithmetic shift right by n bits |

### 12.3 Comparison

These return `bool`. They are used in routing chains, not in `fork` arm conditions.

| Function | Notes |
|----------|-------|
| `eq(n)` | equal |
| `neq(n)` | not equal |
| `gt(n)` | greater than |
| `lt(n)` | less than |
| `gte(n)` | greater than or equal |
| `lte(n)` | less than or equal |

```dagger
~ is_positive :: bool = x -> gt(0)
```

### 12.4 Logic

| Function | Notes |
|----------|-------|
| `and(b)` | logical AND |
| `or(b)` | logical OR |
| `not` | logical NOT |

### 12.5 Text

| Function | Input | Output | Notes |
|----------|-------|--------|-------|
| `text.len` | text | int | byte length |
| `text.at(n)` | text | char | char at byte index n |
| `text.slice(a, b)` | text | text | substring from byte index a to b (exclusive) |
| `text.join(t)` | text | text | concatenate with t |
| `text.find(t)` | text | int? | byte index of first occurrence of t, or null |
| `text.trim` | text | text | strip leading and trailing whitespace |
| `text.split(sep)` | text | slice[text] | split by separator |
| `text.from` | num | text | format number as decimal text |

### 12.6 I/O

| Function | Input | Output | Notes |
|----------|-------|--------|-------|
| `out.write` | text | null | write to stdout |
| `out.writeln` | text | null | write to stdout with trailing newline |
| `out.write_err` | text | null | write to stderr |
| `in.read` | null | text | read one line from stdin (strips newline) |
| `in.read_raw` | null | slice[byte] | read raw bytes from stdin |
| `file.open` | text | file!err | open file at path, return handle or error |
| `file.read` | file | text!err | read entire file contents |
| `file.write` | file, text | null!err | write text to file |
| `file.close` | file | null | close file handle |

### 12.7 Memory

| Function | Notes |
|----------|-------|
| `mem.alloc(n)` | allocate n bytes on the heap; returns `&byte` |
| `mem.realloc(ptr, n)` | resize an existing heap allocation |
| `mem.free(ptr)` | free a heap pointer |
| `mem.copy(src, dst, n)` | copy n bytes from src to dst |
| `mem.zero(ptr, n)` | zero n bytes starting at ptr |

### 12.8 Math

| Function | Notes |
|----------|-------|
| `math.sqrt` | square root |
| `math.floor` | round toward negative infinity |
| `math.ceil` | round toward positive infinity |
| `math.round` | round to nearest integer |
| `math.sin` `math.cos` `math.tan` | trigonometric functions |
| `math.log` `math.log2` `math.log10` | logarithms |

### 12.9 Utility

| Function | Notes |
|----------|-------|
| `cast(type)` | safe widening cast |
| `cast.unsafe(type)` | reinterpret-cast; no safety checks |
| `tee(fn)` | side-route a copy to fn, pass original forward |
| `id` | identity; passes input through unchanged |
| `const(v)` | discard input; emit constant v |
| `assert(cond)` | halt with diagnostic if cond is false |
| `unreachable` | mark a code path as impossible; halts if reached |

---

## 13. Error Handling

Dagger has no exceptions. Errors are values. A function that can fail declares its return type as `T!err`. The caller must explicitly handle or propagate the result.

### 13.1 The Error Type

```dagger
~ result :: int!err    # either an int or an error
```

An `int!err` value is in exactly one of two states: success (holding an `int`) or error (holding an error message). You cannot read the `int` without first handling or propagating the error case.

### 13.2 Typed Errors

Errors carry a `text` tag identifying their kind. To distinguish error cases, produce errors with distinct tags and match on them in `fork`:

```dagger
@fn open_file [~ path :: text] => file!err [
    path -> sys.stat -> fork [
        ?= 0  -> @error("not_found")
        _     ->
            path -> sys.open -> fork [
                ?< 0  -> @error("permission_denied")
                _     -> path -> sys.open
            ]
    ]
]
```

Matching on error tags:

```dagger
result -> fork [
    @ok  -> [~ f]    [ f -> process ]
    @err -> [~ e] [
        e -> fork [
            ?= "not_found"         -> "File does not exist" -> out.write_err
            ?= "permission_denied" -> "Access denied"       -> out.write_err
            _                      -> e -> out.write_err
        ]
    ]
]
```

### 13.3 Producing Errors

```dagger
@fn divide [~ a :: int, ~ b :: int] => int!err [
    b -> fork [
        ?= 0  -> @error("division_by_zero")
        _     -> a -> div(b)
    ]
]
```

`@error("tag")` produces an error value with the given text tag. The tag is a plain `text` value.

### 13.4 Handling Errors

```dagger
~ val = 10, 0 -> divide

val -> fork [
    @ok  -> [~ n] [ n -> out.write ]
    @err -> [~ e] [ e -> out.write_err ]
]
```

`@ok` and `@err` are special `fork` arm conditions for `T!err` types. The bound value in the `@ok` arm has the unwrapped success type (`int` in this case). The bound value in the `@err` arm has type `text`.

### 13.5 Propagating Errors

`@bubble` short-circuits: if the value on its left is an error, it immediately returns that error from the current function. If the value is `@ok`, routing continues with the unwrapped success value.

```dagger
@fn compute [~ x :: float] => float!err [
    x -> safe_sqrt -> @bubble    # exits function with error if safe_sqrt failed
    -> mul(2.0)                  # only reached on success; input is unwrapped float
]
```

The function's declared return type must be `T!err` for `@bubble` to be valid.

### 13.6 Default on Error

```dagger
x -> safe_sqrt -> @or(0.0)    # substitute 0.0 if safe_sqrt returned an error
```

`@or(default)` unwraps a `T!err` value: returns the success value if `@ok`, or the given default if `@err`. The default must have the same type as the success type.

---

## 14. Modules

### 14.1 File as Module

Every `.dag` file is a module. Its module name is its filename without the extension. Directory paths become part of the name: `src/math/vec.dag` is the module `math.vec`.

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

5 -> math.double -> out.write
```

`@use` makes the module's exported functions and `@static` streams available under its name.

### 14.3 Selective Import

```dagger
@use math [ double, square ]

5 -> double -> out.write    # no math. prefix needed
```

### 14.4 Exporting

All top-level `@fn` declarations and `@static` streams are exported by default. Prefix with `@private` to exclude from the module's public interface:

```dagger
@private @fn helper [~ n :: int] => int [
    n -> mul(3)
]

@fn triple [~ n :: int] => int [
    n -> helper
]
```

### 14.5 External C Functions

```dagger
@extern @fn printf [~ fmt :: &byte, ...] => int
@extern @fn malloc [~ size :: uint]      => &byte
```

`@extern` declares a function with C calling convention. The compiler emits a standard `call` instruction and handles ABI-correct argument placement.

---

## 15. Compile-time Functions

`@comptime` marks a function that runs entirely at compile time. Its output is substituted as a constant wherever it is called. Compile-time functions may only operate on literals and compile-time constants — no runtime state.

```dagger
@comptime @fn kilobytes [~ n :: int] => int [
    n -> mul(1024)
]

~ buf :: block[@kilobytes(4)]    # equivalent to block[4096]
```

Compile-time functions are used for: computed buffer sizes, constant folding, and conditional compilation.

```dagger
@comptime @fn is_x86_64 => bool [
    @arch -> eq("x86_64")
]
```

`@arch` is a compile-time built-in that returns the target architecture string.

---

## 16. Assembly Mapping

This section documents the exact x86-64 assembly emitted for each Dagger construct. If the compiler's output deviates from these mappings, it is a compiler bug.

### 16.1 Register Assignment

The compiler uses graph coloring to assign streams to registers. Allocation order (callee-saved registers preferred for long-lived streams):

```
rax rbx rcx rdx rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
```

Stack slots are used when register pressure exceeds available registers. `@pin` annotations override the allocator for a specific stream.

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
; return value in rax
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

### 16.6 Block Entry and Exit

```dagger
block [
    ~ x   :: int      = 5
    ~ buf :: block[64]
]
```
```asm
sub rsp, 72       ; 8 bytes (int x) + 64 bytes (buf)
mov qword [rsp+64], 5
; body
add rsp, 72
```

### 16.7 Explicit Free (@burst on heap value)

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

Syscall functions emit a `syscall` instruction with registers set per the Linux x86-64 ABI: `rax` = syscall number, `rdi rsi rdx r10 r8 r9` = arguments in order.

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
                  | "~" identifier ("," identifier)+ ("::" type)? "=" expression ("," expression)+

route_stmt      ::= expression ("->" expression)+

function_decl   ::= annotation* "@fn" identifier "[" param_list? "]" ("=>" type)? "[" statement* "]"

type_decl       ::= "@type" identifier "[" field_list "]"
                  | "@type" identifier "[" union_list "]"

annotation      ::= "@inline" | "@private" | "@comptime" | "@extern" | "@static"

param_list      ::= param ("," param)*
param           ::= "~" identifier "::" type

field_list      ::= (identifier "::" type newline)*

union_list      ::= ("|" type newline)+

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
                  | each_expr
                  | "[" expression_list "]"

function_call   ::= identifier "(" arg_list? ")"

block_expr      ::= "block" ("." identifier)?
                    ("[" param_list "]")?
                    "[" statement* "]"

fork_expr       ::= "fork" "[" fork_arm+ "]"

fork_arm        ::= fork_cond "->" (expression | block_expr)

fork_cond       ::= "?" comparison_op literal
                  | "@ok"
                  | "@err"
                  | identifier           # union variant name
                  | "_"

loop_expr       ::= "loop" "[" expression "]" "[" statement* "]"
                  | "loop.range" "(" expression "," expression ")"
                    "->" "[" "~" identifier "]" "[" statement* "]"

each_expr       ::= expression "->" "each" "->" "[" "~" identifier "]" "[" statement* "]"

comparison_op   ::= "=" | "!=" | ">" | "<" | ">=" | "<="

type            ::= primitive_type
                  | "block" "[" integer "]"
                  | "slice" "[" type "]"
                  | "fn" "(" type_list "->" type ")"
                  | type "?"
                  | type "!" identifier
                  | "&" type
                  | identifier

type_list       ::= type ("," type)*

primitive_type  ::= "int" | "int8" | "int16" | "int32" | "int64"
                  | "uint" | "uint8" | "uint16" | "uint32" | "uint64"
                  | "float" | "float32"
                  | "bool" | "byte" | "text" | "char" | "null"

literal         ::= integer | float | string | char | "true" | "false" | "null"

directive       ::= "@use" identifier ("[" identifier_list "]")?
                  | "@burst" identifier
                  | "@error" "(" string ")"
                  | "@bubble"
                  | "@or" "(" expression ")"
                  | "@break"
                  | "@skip"
                  | "@syscall" function_decl

identifier_list ::= identifier ("," identifier)*

expression_list ::= expression ("," expression)*

arg_list        ::= expression ("," expression)*
```

---

## 18. Standard Library

The Dagger standard library (`@use std`) is written in Dagger. There is no privileged runtime — `std` is a collection of `.dag` files.

### 18.1 std.io

I/O functions for files, stdin/stdout/stderr, and formatting.

### 18.2 std.mem

Memory allocation. The default allocator wraps `malloc`/`free`. Arenas and pool allocators are provided as alternatives.

```dagger
@use std.mem

~ arena = std.mem.arena(4096)              # create a 4 KB arena
~ buf   = arena -> std.mem.arena.alloc(128) # allocate 128 bytes from arena
arena -> std.mem.arena.free               # free entire arena at once
```

To use a custom allocator as the default for `@heap` declarations, set `std.mem.default_allocator` at program start.

### 18.3 std.math

Full math library: trigonometry, logarithms, random number generation.

### 18.4 std.text

Text manipulation: parsing, formatting, searching, encoding/decoding.

### 18.5 std.collections

```dagger
@use std.collections

~ list :: std.collections.list[int]
42    -> list -> std.collections.list.push
list  -> std.collections.list.len -> out.write
```

Available: `list`, `map`, `set`, `queue`, `stack`, `ring`.

### 18.6 std.sys

Direct system call wrappers. Platform-specific backends for Linux, macOS, and Windows.

```dagger
@use std.sys
std.sys.exit(0)
```

### 18.7 std.fmt

Format values into text.

```dagger
@use std.fmt
42, "the answer is {}" -> std.fmt.format -> out.write
```

---

## 19. Compiler Pipeline

The `dagc` compiler transforms `.dag` source into a native binary in six stages.

**Stage 1 — Lex:** source text → token stream. Handles comments, literals, operators, identifiers.

**Stage 2 — Parse:** token stream → AST. Validates grammar per Section 17. Produces a tree of routing nodes, block nodes, and function nodes.

**Stage 3 — Type Resolution:** AST → type-annotated AST. Infers types per Section 6.4. Reports type mismatches. Resolves function overloads where applicable.

**Stage 4 — Value Lowering:** type-annotated AST → Stream IR (SIR). SIR is a flat SSA-like representation where all values have explicit lifetimes, all block boundaries are explicit enter/exit operations, all routing chains are flattened to binary operations, and all `@burst` and scope-exit free points are explicit.

**Stage 5 — Register Allocation:** SIR → register-allocated IR. Graph coloring assigns streams to registers or stack slots. `@pin` annotations are enforced. Spill code is inserted where needed.

**Stage 6 — Code Generation:** register-allocated IR → x86-64 assembly (AT&T or Intel syntax, configurable). Assembled via NASM/GAS or the internal assembler to an object file, then linked to produce the final binary.

### Compiler Flags

```
dagc [flags] <source files>

-o <file>         output binary name (default: a.out)
-S                emit assembly only, do not assemble or link
-O0/O1/O2/O3      optimization level (default: O2)
--syntax intel    emit Intel syntax assembly (default: AT&T)
--no-stdlib       do not link the standard library
--emit-sir        dump Stream IR before register allocation
--verbose         print stage timings
--check           type-check only; no output produced
```

---

## 20. Examples

### 20.1 FizzBuzz

```dagger
# fizzbuzz.dag

~ i :: int = 1

loop [i -> lte(100)] [
    i -> fork [
        ?= 0 (i -> mod(15))  -> "FizzBuzz" -> out.writeln
        ?= 0 (i -> mod(3))   -> "Fizz"     -> out.writeln
        ?= 0 (i -> mod(5))   -> "Buzz"     -> out.writeln
        _                    -> i -> text.from -> out.writeln
    ]
    i -> add(1) -> i
]
```

**Note on fork conditions with expressions:** when the condition operand is not a literal but a routed expression, wrap it in parentheses: `?= 0 (i -> mod(15))`. The parenthesised expression is evaluated first; its result is compared against the literal. This is the only context where a fork arm condition operand may be an expression rather than a literal.

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

### 20.4 Struct and Functions

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

buf[0], 16 -> sys.write(1)    # fd=1 (stdout)

@burst buf
std.sys.exit(0)
```

---

## Appendix A: Keyword Reference

| Keyword | Meaning |
|---------|---------|
| `~` | Declare a stream |
| `->` | Route data left to right |
| `=>` | Declare function return type |
| `::` | Annotate type |
| `?x` | Probe stream x (non-consuming read) |
| `!x` | Burst stream x (consuming read + free) |
| `?= ?!= ?> ?< ?>= ?<=` | Fork arm conditions (only valid inside fork) |
| `_` | Wildcard / discard |
| `>>` | Compose two functions |
| `@fn` | Declare a function |
| `@type` | Declare a struct or union type |
| `@burst` | Explicitly free a stream |
| `@static` | Declare a program-lifetime stream |
| `@inline` | Inline function at all call sites |
| `@extern` | Declare an external C ABI function |
| `@comptime` | Declare a compile-time function |
| `@private` | Exclude from module exports |
| `@use` | Import a module |
| `@error(tag)` | Produce an error value with the given text tag |
| `@bubble` | Propagate error from current routing position |
| `@or(default)` | Unwrap success value or substitute default on error |
| `@ok` / `@err` | Fork arm conditions for error types |
| `@break` | Exit the enclosing loop or each |
| `@skip` | Advance to the next iteration |
| `@syscall` | Declare a direct syscall function |
| `loop` | Loop construct |
| `loop.range(a, b)` | Loop over integer range [a, b) |
| `fork` | Branch construct |
| `block` | Explicit scope block |
| `each` | Iterate a slice |
| `tee` | Side-route a copy, pass original forward |
| `null` | Absence of value (valid only in optional types) |
| `true` / `false` | Boolean literals |

---

## Appendix B: Error Messages

The compiler produces errors in the following format:

```
[file]:[line]:[col] error: <message>
  <source line>
  <caret>
  hint: <suggestion>
```

Example:

```
main.dag:12:5 error: stream 'x' has been burst and cannot be probed
  x -> out.write
  ^
  hint: remove the @burst on line 8, or copy x before bursting
```

---

## Appendix C: Reserved Keywords

The following are reserved for future versions and may not be used as identifiers:

```
async await spawn chan select
match where impl trait
gpu kernel
```

---

*Dagger language specification — version 0.1.0-draft*  
*All syntax is subject to revision during implementation.*
