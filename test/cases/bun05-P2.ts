// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import type { Symbol } from "./ty";
const k1 = Symbol();
const k2 = Symbol();
const o = { [k1]: 1, [k2]: "a" };
const n: number = o[k1];
const sentinel: number = "";
// @filename: ty.ts
export interface Symbol { q: 1 }
