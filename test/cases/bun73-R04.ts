// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { L3 } from "./b";
declare const u: unknown;
// an ambient class: no TS2449
const k1 = u instanceof L1 ? "s" : "s";
declare class L1 { static [k1]: number; i: number }
const p1: never = L1.s;
// a class in another file
export const k3 = u instanceof L3 ? "s" : "s";
const p3: never = L3.s;
// with TS2449
const k2 = u instanceof L2 ? "s" : "s";
class L2 { static [k2] = 1; i = 1 }
const p2: never = L2.s;
const sentinel: number = "";
// @filename: b.ts
import { k3 } from "./a";
export class L3 { static [k3] = 1; i = 1 }
const sentinel: number = "";
