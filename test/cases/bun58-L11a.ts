// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": ["foo"], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: __inferred type names__.ts
export const c = 1; const inner: number = "";
// @filename: a.ts
import { c } from "./__inferred type names__";
const f: 0 = foo;
const g: 0 = c;
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
declare const foo: 1;
