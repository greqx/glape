# Benchmark - FFI

1,000,000 calls to libc strlen via FFI.

## Setup

Compile libc.gdl first:

```bash
glape gdl -sys /lib64/libc.so.6 --map libc.gffi --output libc.gdl
```

With libc.gffi:

```
strlen(str) >> int
```

## Results

| Date       | OS        | CPU              | Version  | Glape  | Python3 ctypes |
|------------|-----------|------------------|----------|--------|----------------|
| 2026-09-20 | Fedora 44 | AMD PRO A10 8770 | 1.0-beta2 | 0.094s | 0.573s         |

Contributors, if you have nothing better to do - please run the bench and share your results :)

## How to run

```bash
time ./build/glape examples/bench/ffi/bench.glape
time python3 examples/bench/ffi/bench.py
```
