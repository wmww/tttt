# The Tess TypeScript Type checker

A high speed [TypeScript](https://www.typescriptlang.org/) type checker written in C. tttt is a drop-in replacement for `tsc --noEmit`, it understands the same flags and tsconfigs. See `tttt --help` for more info.

To use in your project:
```bash
npm install --save-dev tttt-ts
npx tttt
# or ./node_modules/tttt-ts-linux-x64/bin/tttt for faster startup
```

Or install globally/[download from GitHub](https://github.com/wmww/tttt/releases).

To build from source:
```bash
# install GCC 13+, meson, ninja and (on Linux) llvm-bolt
scripts/fastbuild.sh
fast/tttt -p path/to/project
```
