// @strict: true
// @module: commonjs
// @lib: es2022
// A generic reference node lowered inside a cycle holds that history's types: `B<typeof y>` lowered while checking
// m.ts first reads y mid-cycle (its own private value). With the shared graph its lowering (KEY_UNION_NODE) was
// published whatever its taint, so a checker of a.ts read `B<number>` and never met the cycle: no TS7022 at y, no
// TS2502 at p, `number` where one checker has `any` (bun 85-R16b, K2 with m.ts and a.ts on two checkers). The suite
// runs one checker: this one fails without the fix only in the K > 1 dumps (oracles.py's kdiff)
// @filename: m.ts
import { y } from "./a";
interface B<T> { b: T }
interface V extends B<typeof y> { p: number }
declare const v: V;
export = v;
// @filename: a.ts
import { p } from "./m";
export const y = p;
const t: never = y;
const sentinel: number = "";
