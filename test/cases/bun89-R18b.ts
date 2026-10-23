// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { a1 } from "./v";
export interface I1 extends Array<typeof a1> {}
export declare class Base<T> { inherited: T }
const sentinel: number = "";
// @filename: v.ts
// Before a.ts in program order: tsc types the variable first, as with the variable above the interface in one file. No TS2339.
import type { I1 } from "./a";
declare const i1: I1;
export const a1 = i1.length;
