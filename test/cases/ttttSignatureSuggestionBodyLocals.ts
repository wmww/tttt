// A name a function's signature doesn't find is suggested only what the signature could see: not the body's variables
// from the parameters, nor its local types, but its `const`s from the return type (tsgo's resolveNameHelper filters the
// suggested symbol like a found one). tttt suggested `Count` for all of them; deferred bodies (plan 78) restarted the
// program for any unknown signature name (`Buffer` without @types/node) whose function declared a local
// @filename: a.ts
export function g(x: typeof Coutn) { const Count = 1; return x; }
export function h(x = Coutn) { const Count = 1; return x; }
export function k(x: Coutn) { type Count = 1; return x; }
export function r(): typeof Totl { const Total = 1; return Total; }
export function v(): typeof Totl { var Total = 1; return Total; }
export function far(x: Buffr) { const unrelated = 1; return x; }
// @filename: b.ts
import { r } from "./a";
export const a: string = r();
