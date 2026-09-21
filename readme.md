# Glape
A minimal scripting language for Unix.

```
x:int = 5 + 3
print(x)
```

## Philosophy

- **Small** - the interpreter binary is very small.
- **Low memory** - striving for minimal RAM consumption.
- **Unix only** - no portability overhead, built for Linux and macOS.

<!-- ## Website and documentation
A base information and download language: 
[.com/glape/](https://greqx.github.com/glape)

Documentation of using:
[.com/glape/docs](https://greqx.github.com/glape/docs) -->

## Build
Use `make` command. Binaries land in `build/`:

```bash
build/glape          # run scripts
build/glape-daemon   # daemon control
```

## Usage

```bash
# run a script
build/glape script.glape

# compile a library folder to gdl (see docs)
build/glape gdl mylib/ --output mylib.gdl

# daemon
build/glape-daemon start
build/glape-daemon status
build/glape-daemon stop
```

## License
Apache License 2.0 - see LICENSE
Copyright (c) 2026 greqx and Contributors
