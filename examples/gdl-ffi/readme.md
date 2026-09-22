## How to run this test?
Compile .gffi to .gdl:
```bash
./build/glape gdl -sys /lib64/libc.so.6 --map libc.gffi --output libc.gdl
```
Use gdl-dump:
```bash
./build/glape gdl-dump libc.gdl
```
Result:
```
type: FFI
so: /lib64/libc.so.6
symbols: 3

  puts(str) >> int
  strlen(str) >> int
  atoi(str) >> int
```
