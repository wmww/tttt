# The Tess TypeScript Type checker

A high speed [TypeScript](https://www.typescriptlang.org/) type checker written in C, using [cleat](https://codeberg.org/sphi/cleat).

Install (Linux x86-64, glibc 2.38+; other platforms are coming):
```bash
npm install --save-dev tttt-ts
npx tttt -p path/to/project
```
or download a release's tarball from GitHub (binary and its lib files: keep `bin/` and `thirdparty/` together).

Build dependencies:
- GCC 13+ (tested up to GCC 16)
- meson+ninja
- llvm-bolt

Build and run:
```bash
scripts/fastbuild.sh
fast/tttt -p path/to/project # or run from your project directory without arguments
```

tttt is meant as a drop-in `tsc` for checking: the same command lines and tsconfigs. Every run behaves like `tsc --noEmit`, except that a project whose config has `emitDeclarationOnly` gets its `.d.ts` and `.d.ts.map` files written like `tsc` does. See `release/tttt --help` for more info.
