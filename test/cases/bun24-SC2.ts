// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { o, sym } from "./m";
export const x: number = { a: o };
export const sentinel: number = "";
// @filename: m.ts
export const sym = Symbol(); export const o = { [sym]: 1 };
