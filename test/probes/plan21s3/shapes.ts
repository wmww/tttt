declare const b: { x: number };
interface I { a: string }
namespace N { export interface Q { q: number } export type R = string }
function f1(x: typeof b, y: Array<string>, z: ReadonlyArray<I>): typeof b { return b; }
function f2(x: (() => string) | number, y: {a:string,b?:number}, z: [a: string, b?: number, ...rest: boolean[]]): keyof I { return "a"; }
function f3<T extends { k: string }, U = number>(x: T["k"], y: N.Q, z: 'lit' | "dq" | 0x10 | -1 | 1e3 | true | null | undefined): void {}
function f4(x: new (a: string) => I, y: abstract new () => object, z: `a${string}b`): unknown { return 0; }
function f5(x: { [k: string]: number }, y: { readonly [K in keyof I]?: I[K] }, z: I extends string ? 1 : 2): never { throw 0; }
function f6(x: { (a: number): string; new (): I; m(): void; m2?<T>(t: T): T; readonly p: number }): symbol { return Symbol(); }
function f7(x: string [], y: readonly string[], z: (string | number)[], w: import("./other").O): bigint { return 0n; }
function f8(x: this | void, y: Uppercase<"a">, z: Omit<I, "a">, w: Partial<I>) {}
function f9(...args: [number, string?]) {}
function f10(x?: string, y: number = 1) {}
const g = (a: { a: string, }) => a;
class C { m(x: typeof b): typeof x { return x; } p: typeof b = b; }
let v1: Array<string> = [];
let v2: (() => void)[] = [];
