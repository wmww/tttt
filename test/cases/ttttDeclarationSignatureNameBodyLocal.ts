// @declaration: true
// @strict: true
// @target: es2022
// @module: nodenext
// A name in a function declaration's signature resolves, for declaration emit's visibility check, from the function
// (tsc's isEntityNameVisible at the enclosing declaration): its body's top-level declarations and vars shadow an outer
// import (openclaw's `typeof directSessionReq` and a `const { directSessionReq } = await import(...)` in the body,
// TS4078); a block's own names, a parameter and an arrow function's body don't
// @filename: package.json
{ "type": "module" }
// @filename: helper.ts
export function helper(a: number, b: string): number { return a; }
export type Opts = { a: number };
// @filename: main.ts
import type { helper, Opts } from "./helper.js";
export async function f1(...args: Parameters<typeof helper>) {
    const { helper } = await import("./helper.js");
    return helper(...args);
}
export function f2(o: Opts): Opts {
    type Opts = string;
    return o;
}
export function f3(): ReturnType<typeof helper> {
    if (Math.random()) { var helper = (x: number) => x; }
    return 1;
}
export function f4(x: Parameters<typeof helper>[0]) {
    { const helper = 1; }
    return x;
}
export function f5(helper: number, y: typeof helper) { return y; }
export class C {
    m(x: Parameters<typeof helper>[0]) { const helper = 1; return x + helper; }
}
export const f6 = (x: Parameters<typeof helper>[0]) => { const helper = 1; return x + helper; };
export function f7<T extends Opts>(x: T) { interface Opts { b: 1 } return x; }
export function f8(x: Parameters<typeof helper>[0]) { function helper(q: number) {} return x; }
