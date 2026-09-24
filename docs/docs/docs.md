# Glape Documentation

## Table of contents

- [Getting started](#getting-started)
- [Syntax](#syntax)
- [Types](#types)
- [Variables](#variables)
- [Conditions](#conditions)
- [Loops](#loops)
- [Functions](#functions)
- [Libraries](#libraries)
- [FFI](#ffi)
- [Daemon](#daemon)
- [CLI reference](#cli-reference)

---

## Getting started

### Build from source

```bash
git clone https://github.com/greqx/glape
cd glape
make
```

Two binaries are placed in `build/`:

- `build/glape` — runs `.glape` scripts
- `build/glape-daemon` — manages running scripts in the background

Requires GCC and GNU Make. Tested on Linux and macOS.

### Hello world

```
name:str = "world"
print(name)
```

```bash
./build/glape hello.glape
```

```
world
```

---

## Syntax

Blocks are indented, not braced. Two spaces per level.

```
if x > 0:
  print(x)
  x = x - 1
```

No `{`, no `end`, no `;`.

### Comments

`#` starts a comment. Everything after it on the same line is ignored.

```
# full-line comment
x:int = 5  # inline comment
```

### Errors

Errors include the exact position:

```
error: cannot assign str to int  (4:7)
```

`(line:column)`. All errors are caught before any code runs.

---

## Types

| Type    | Description           | Examples              |
|---------|-----------------------|-----------------------|
| `int`   | 64-bit signed integer | `0`, `42`, `-7`       |
| `float` | 64-bit float          | `3.14`, `-0.5`, `1.0` |
| `str`   | UTF-8 string          | `"hello"`, `""`       |
| `bool`  | boolean               | `true`, `false`       |

Types are required on every declaration. There is no inference.

### int and float

`int` is promoted to `float` when mixed in an expression:

```
x:int   = 3
y:float = 1.5
z:float = x + y   # 4.5
```

---

## Variables

```
x:int     = 10
pi:float  = 3.14159
msg:str   = "hello"
ok:bool   = true
```

Variables are mutable. The type cannot change:

```
x:int = 5
x = x + 1   # 6
x = "oops"  # error: cannot assign str to int
```

### Immutable variables

`immut` prevents reassignment:

```
immut max_size:int = 1024
max_size = 2048  # error: cannot assign to immutable variable 'max_size'
```

Good for constants you don't want touched later.

---

## Conditions

```
x:int = 7

if x > 5:
  print(x)
```

```
7
```

### else

```
x:int = 3

if x > 5:
  print("big")
else:
  print("small")
```

```
small
```

### Operators

| Operator | Meaning               |
|----------|-----------------------|
| `==`     | equal                 |
| `!=`     | not equal             |
| `>`      | greater than          |
| `<`      | less than             |
| `>=`     | greater than or equal |
| `<=`     | less than or equal    |

Works on strings too:

```
name:str = "glape"

if name == "glape":
  print("correct")
```

---

## Loops

`loop` is the only loop keyword. Three forms.

### Infinite

```
loop:
  print("running")
```

No condition, runs forever. No `break` yet — exit by returning from a function or killing the process.

### Conditional

```
x:int = 5

loop x > 0:
  print(x)
  x = x - 1
```

```
5
4
3
2
1
```

Condition is checked at the top of each iteration.

### Range

`..` excludes the end, `..=` includes it:

```
loop i in 1..5:
  print(i)
# 1 2 3 4

loop i in 1..=5:
  print(i)
# 1 2 3 4 5
```

### Nesting

```
loop i in 1..=3:
  loop j in 1..=3:
    print(i)
```

Prints `1` three times, then `2`, then `3`.

---

## Functions

```
define add(a:int, b:int) >> int:
  return a + b

result:int = add(3, 4)
print(result)
```

```
7
```

`>> int` is the return type. All parameters need types.

### No return value

Leave out `>>`:

```
define greet(name:str):
  print(name)

greet("glape")
```

### Calling other functions

```
define square(x:int) >> int:
  return x * x

define sum_of_squares(a:int, b:int) >> int:
  return square(a) + square(b)

print(sum_of_squares(3, 4))
```

```
25
```

### Scope

Functions don't see variables from outer scope. Pass what you need as arguments.

---

## Libraries

A library is a folder of `.glape` files compiled into a `.gdl` binary. A typical library is a few hundred bytes.

### Writing a library

```
mathlib/
  math.glape
  utils.glape
```

`math.glape`:

```
define square(x:int) >> int:
  return x * x

define cube(x:int) >> int:
  return x * x * x
```

`utils.glape`:

```
define max(a:int, b:int) >> int:
  if a > b:
    return a
  return b
```

### Compiling

```bash
./build/glape gdl mathlib/ --output mathlib.gdl
```

```
compiling 'mathlib' -> 'mathlib.gdl'
  + module 'math'
  + module 'utils'
done
```

### Using

Put `mathlib.gdl` next to your script:

```
get mathlib

x:int = mathlib.square(5)
print(x)

y:int = mathlib.max(3, 7)
print(y)
```

```
25
7
```

All functions from all files in the folder are available under the library name. The source file doesn't matter to the caller.

### Search path

`get mylib` looks in this order:

1. Same directory as the script
2. `libs/` next to the script
3. `~/.glape/libs/`

### Dependencies

If your library needs another library, put the `.gdl` in `mylib/libs/`. It gets embedded into the compiled output:

```
mylib/
  libs/
    otherlib.gdl
  main.glape
```

The resulting `.gdl` is self-contained.

### Inspecting

```bash
./build/glape gdl-dump mathlib.gdl
```

Prints the bytecode of every module in the file.

---

## FFI

FFI calls functions from `.so` shared libraries directly, with no interpreter overhead.

### 1. Write a .gffi file

List the C functions you need:

```
# libc.gffi
puts(str) >> int
strlen(str) >> int
malloc(int) >> ptr
free(ptr)
atoi(str) >> int
```

Format: `function_name(arg_types) >> return_type`

| Type   | C equivalent  | Glape side |
|--------|---------------|------------|
| `int`  | `int64_t`     | `VAL_INT`  |
| `str`  | `const char*` | `VAL_STR`  |
| `ptr`  | `void*`       | `VAL_INT`  |
| `void` | `void`        | nothing    |

`ptr` is stored as an integer (a raw memory address). Pass it back to functions that take a pointer.

### Symbol aliases

To expose a C function under a different name in Glape, use `=`:

```
my_alloc=malloc(int) >> ptr
```

`libc.my_alloc(1024)` calls `malloc`.

### 2. Compile to .gdl

```bash
./build/glape gdl -sys /lib64/libc.so.6 --map libc.gffi --output libc.gdl
```

Verifies all symbols exist in the `.so`, then writes `libc.gdl`.

### 3. Use in a script

```
get libc

libc.puts("hello from FFI!")

n:int = libc.strlen("glape")
print(n)

x:int = libc.atoi("42")
print(x)
```

```
hello from FFI!
5
42
```

### Inspecting

```bash
./build/glape gdl-dump libc.gdl
```

```
type: FFI
so: /lib64/libc.so.6
symbols: 5

  puts(str) >> int
  strlen(str) >> int
  malloc(int) >> ptr
  free(ptr) >> void
  atoi(str) >> int
```

### Limitations

- Max 3 arguments per function
- `float` is not supported in FFI signatures
- No variadic functions. `printf` won't work; use `puts` or build the string in Glape first

---

## Daemon

The daemon runs in the background and accepts scripts over a Unix socket. Once started, `glape script.glape` sends the script to the daemon rather than running it locally.

### Start and stop

```bash
./build/glape-daemon start
./build/glape-daemon stop
./build/glape-daemon restart
./build/glape-daemon status
```

```
glape-daemon running  pid=1234  scripts=2
```

### Managing scripts

```bash
./build/glape-daemon list
./build/glape-daemon kill 5678
```

```
pid=5678
pid=5701
```

### Skip the daemon

To run locally even when the daemon is up:

```bash
./build/glape --local script.glape
```

### Files

All daemon files live in `~/.glape/`:

- `d.sock` — Unix socket
- `d.pid` — PID file
- `d.log` — log output

---

## CLI reference

### glape

```
glape <file.glape>
    Run a script. Uses the daemon if running, otherwise local.

glape <file.glape> --local
    Always run locally.

glape gdl <folder/> --output <file.gdl>
    Compile a library folder into a .gdl binary.

glape gdl -sys <lib.so> --map <file.gffi> --output <file.gdl>
    Compile a .so + .gffi into an FFI .gdl library.

glape gdl-dump <file.gdl>
    Print the contents of a .gdl file.

glape gffi-dump <file.gffi>
    Print the parsed contents of a .gffi file.

glape --version
glape --help
```

### glape-daemon

```
glape-daemon start
glape-daemon stop
glape-daemon restart
glape-daemon status
glape-daemon list
glape-daemon kill <pid>
glape-daemon --version
glape-daemon --help
```

