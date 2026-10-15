//// [test/cases/ttttDeclarationMaps.ts] ////

//// [b.ts]
export type X = number;

//// [a.ts]
/** doc */
export interface I<T extends string = "a"> extends Base {
    readonly a?: number;
    b(x: T, ...rest: string[]): void;
    [key: string]: any;
    new (): I<T>;
}
interface Base { z: string | number[] }
export declare function f(a: number, b?: string): Promise<void>;
export function g(a = 1, { b }: { b: string }) { return a; }
export const c = 1, d = "x";
export let e = [1, 2];
export class C<U> extends Array<U> implements Base {
    z = "";
    private p!: number;
    constructor(public q: number, readonly r?: string) { super(); }
    get x(): number { return 1; }
    set x(v) {}
    static m<V>(v: V): V { return v; }
    private async n() {}
    #priv = 1;
}
export type A = { a: 1; b: [x: number, y?: string] } | keyof Base | typeof c | -1;
export enum E { A = 1, B, C = "c" }
export namespace N.M { export const x = 1; }
export default class { }
export { Base as B2 };
import type { X } from "./b";
export type Y = X;
export abstract class D { abstract m(): void; protected n?(): this; }
type M<T> = { readonly [K in keyof T]-?: T[K] extends infer R ? R : never };
export type M2 = M<{a: 1}>;
export declare const t: `a${string}`;
export const fn = (self: Iterable<unknown>, x?: number, ...r: string[]): self is Iterable<never> => true;
export const gn = <A, B>(self: Iterable<readonly [A, B]>): Iterable<A> => null!;
export const o = { a: 1, b(x: number): string { return "" }, get c(): number { return 1 }, "d-e": 2, n: -5 };
export function h({ a, b: [c] }: { a: number, b: [string] }, [d]: number[]) {}
export class K { [Symbol.iterator](): Iterator<number> { return null! } }
export const s = Symbol();
export declare function assertNode(node?: object | null): asserts node is Object;
export type T2 = { [K in "a" | "b" as `x${K}`]-?: K };
export interface Variance<in out V, const W> { v: V; w: W }
export type Ctor = abstract new (...args: any) => any;
export type P = typeof import("./b");
export function expando() {}
expando.prop = 1;


//// [b.js]
export {};
//// [a.js]
export function g(a = 1, { b }) { return a; }
export const c = 1, d = "x";
export let e = [1, 2];
export class C extends Array {
    q;
    r;
    z = "";
    p;
    constructor(q, r) {
        super();
        this.q = q;
        this.r = r;
    }
    get x() { return 1; }
    set x(v) { }
    static m(v) { return v; }
    async n() { }
    #priv = 1;
}
export var E;
(function (E) {
    E[E["A"] = 1] = "A";
    E[E["B"] = 2] = "B";
    E["C"] = "c";
})(E || (E = {}));
export var N;
(function (N) {
    var M;
    (function (M) {
        M.x = 1;
    })(M = N.M || (N.M = {}));
})(N || (N = {}));
export default class {
}
export class D {
}
export const fn = (self, x, ...r) => true;
export const gn = (self) => null;
export const o = { a: 1, b(x) { return ""; }, get c() { return 1; }, "d-e": 2, n: -5 };
export function h({ a, b: [c] }, [d]) { }
export class K {
    [Symbol.iterator]() { return null; }
}
export const s = Symbol();
export function expando() { }
expando.prop = 1;


//// [b.d.ts]
export type X = number;
//# sourceMappingURL=b.d.ts.map//// [a.d.ts]
/** doc */
export interface I<T extends string = "a"> extends Base {
    readonly a?: number;
    b(x: T, ...rest: string[]): void;
    [key: string]: any;
    new (): I<T>;
}
interface Base {
    z: string | number[];
}
export declare function f(a: number, b?: string): Promise<void>;
export declare function g(a: number | undefined, { b }: {
    b: string;
}): number;
export declare const c = 1, d = "x";
export declare let e: number[];
export declare class C<U> extends Array<U> implements Base {
    #private;
    q: number;
    readonly r?: string | undefined;
    z: string;
    private p;
    constructor(q: number, r?: string | undefined);
    get x(): number;
    set x(v: number);
    static m<V>(v: V): V;
    private n;
}
export type A = {
    a: 1;
    b: [x: number, y?: string];
} | keyof Base | typeof c | -1;
export declare enum E {
    A = 1,
    B = 2,
    C = "c"
}
export declare namespace N.M {
    const x = 1;
}
export default class {
}
export { Base as B2 };
import type { X } from "./b";
export type Y = X;
export declare abstract class D {
    abstract m(): void;
    protected n?(): this;
}
type M<T> = {
    readonly [K in keyof T]-?: T[K] extends infer R ? R : never;
};
export type M2 = M<{
    a: 1;
}>;
export declare const t: `a${string}`;
export declare const fn: (self: Iterable<unknown>, x?: number, ...r: string[]) => self is Iterable<never>;
export declare const gn: <A, B>(self: Iterable<readonly [A, B]>) => Iterable<A>;
export declare const o: {
    a: number;
    b(x: number): string;
    readonly c: number;
    "d-e": number;
    n: number;
};
export declare function h({ a, b: [c] }: {
    a: number;
    b: [string];
}, [d]: number[]): void;
export declare class K {
    [Symbol.iterator](): Iterator<number>;
}
export declare const s: unique symbol;
export declare function assertNode(node?: object | null): asserts node is Object;
export type T2 = {
    [K in "a" | "b" as `x${K}`]-?: K;
};
export interface Variance<in out V, const W> {
    v: V;
    w: W;
}
export type Ctor = abstract new (...args: any) => any;
export type P = typeof import("./b");
export declare function expando(): void;
export declare namespace expando {
    var prop: number;
}
//# sourceMappingURL=a.d.ts.map