# Benchmark

Sum of squares from 0 to 1,000,000.

## Results

| Date       | OS        | CPU              | Version  | Glape  | Python3 |
|------------|-----------|------------------|----------|--------|---------|
| 2026-09-20 | Fedora 44 | AMD PRO A10 8770 | 1.0-beta | 0.122s | 0.314s  |

Contributors, if you have nothing better to do - please run the bench and share your results :)

## How to run

```bash
time ./build/glape examples/bench/bench.glape
time python3 examples/bench/bench.py
```
