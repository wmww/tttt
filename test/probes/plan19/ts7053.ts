// @strict: true
function f1(input: object, key: string) { return input[key]; }
function f2(input: {}, key: string) { input[key] = 1; }
function f3<T>(t: T, k: string) { return t[k]; }
function f4<T extends object>(t: T, k: string) { return t[k]; }
function f5(s: string, k: string) { return s[k]; }
function f6(a: number[], k: string) { return a[k]; }
function f7(o: { a: 1 } | { a: 2, b: 1 }, k: "a" | "b") { return o[k]; }
function f8(o: { abc: 1 }) { return o["abd"]; }
class C { static s = 1; x = 1; }
function f9(c: C) { return c["s"]; }
function f10(o: { a: 1 } | undefined, k: string) { return o?.[k]; }
const lit = { a: 1, b: "x" }["a" as string];
const lit2 = { a: 1, b: "x" }[1 as number];
declare const g: typeof globalThis;
const g1 = g["nope"];
const g2 = globalThis["nope2"];
function f11(this: { get(k: string): number }) { return this["x"]; }
declare const fn: () => void;
const fn1 = fn["x"];
declare const m: Map<string, number>;
const m1 = m["x"];
