// origin: npm kirka@6.1.0 https://github.com/numfin/kirka tsconfig.bench.json seed 20261035
// flip: yes
// @lib: esnext, dom
// @module: nodenext
// @moduleResolution: nodenext
// @noImplicitAny: true
// @preserveConstEnums: true
// @strict: true
// @target: esnext
// @moduleDetection: force
// @filename: package.json
{"name": "kirka", "version": "6.1.0", "type": "module", "exports": {".": {"import": "./dist/index.js", "require": "./dist/index.cjs", "types": "./dist/index.d.ts"}, "./*": {"import": "./dist/*.js", "require": "./dist/*.cjs", "types": "./dist/*.d.ts"}}, "types": "dist/index.d.ts", "module": "dist/index.js"}
// @filename: src/result/api/debug.ts
export function debug<T, E>(
  { ok = (v) => v, err = (e) => e } = {} as {
    ok?(v: T): string;
    err?(e: E): string;
  }
) {
}
