// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { assertIsString, fail, Promise, Obj } from "./real";
function t1(x: unknown) { assertIsString(x); x.length; }
function t2(x: string | undefined) { if (!x) fail(); x.length; }
function t3(): number { fail(); }
function t4(x: unknown) { Obj.assert(x); x.length; }
new Promise<number>(resolve => resolve());
const sentinel: number = "";
// @filename: globals.d.ts
declare function assertIsString(x: unknown): asserts x is string;
declare function fail(): never;
declare const Obj: { assert(x: unknown): asserts x is string };
// @filename: real.ts
namespace g2 {
  export interface assertIsString { t: 1 } export interface fail { t: 1 } export interface Promise { t: 1 } export interface Obj { t: 1 }
}
const g2 = { v: 1 };
export = g2;
