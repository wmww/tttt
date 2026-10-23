// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "noUnusedLocals": true, "noUnusedParameters": true } }
// @filename: a.ts
import { C, v } from "./m";
declare const o: any;
const local1 = 1;
function fn1() {}
with (o) { var a1 = new C(), a2 = v + local1, a3 = fn1(); }
a1; a2; a3;
with (o) { var a4 = function (x: number, z: number) { let q = 1, r = 2; return x + q; }; }
a4;
export const sentinel: number = "";
// @filename: m.ts
export class C {} export const v = 1;
