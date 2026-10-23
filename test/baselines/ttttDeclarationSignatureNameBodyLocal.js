//// [test/cases/ttttDeclarationSignatureNameBodyLocal.ts] ////

//// [package.json]
{ "type": "module" }
//// [helper.ts]
export function helper(a: number, b: string): number { return a; }
export type Opts = { a: number };
//// [main.ts]
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


//// [helper.js]
export function helper(a, b) { return a; }
//// [main.js]
export async function f1(...args) {
    const { helper } = await import("./helper.js");
    return helper(...args);
}
export function f2(o) {
    return o;
}
export function f3() {
    if (Math.random()) {
        var helper = (x) => x;
    }
    return 1;
}
export function f4(x) {
    {
        const helper = 1;
    }
    return x;
}
export function f5(helper, y) { return y; }
export class C {
    m(x) { const helper = 1; return x + helper; }
}
export const f6 = (x) => { const helper = 1; return x + helper; };
export function f7(x) {  return x; }
export function f8(x) { function helper(q) { } return x; }


//// [helper.d.ts]
export declare function helper(a: number, b: string): number;
export type Opts = {
    a: number;
};
