// @strict: true
// @target: es2022
// @noEmit: true
// An overload's parameter initializer is TS2371, and is checked only when the overload is compared with its
// implementation (which types its parameters), through getQuickTypeOfExpression first: a literal, an `await` of one, a
// type assertion (its type's names) or a call of a single non-generic signature (its callee) aren't checked further

// @filename: m.ts
export class X { static readonly y = "k"; }

// @filename: a.ts
import type { X } from "./m";
declare function one(x: number): string;
declare let maybe: (() => string) | undefined;
function n1(a = X.y): void; function n1(a: any) {}
function q1(a = await 1): void; function q1(a: any) {}
function q2(a = (await "s")): void; function q2(a: any) {}
function r1(a = await -1): void; function r1(a: any) {}
function r2(a = [await 1]): void; function r2(a: any) {}
function c1(a = one(zzz)): void; function c1(a: any) {}
function c2(a = nope(1)): void; function c2(a: any) {}
function c3(a = zzz as Missing): void; function c3(a: any) {}
function c5(a = maybe()): void; function c5(a: any) {}
function c6(a: number = zzz): void; function c6(a: any) {}
function c9(a = zzz, b = yyy): string; function c9(a: any, b: any) { return 1 as any; }
function c10(a = zzz): void; function c10(a: any, b: any) {}
declare function c11(a = zzz, b = await 1): void;
interface I { m(a = zzz): void }
type T = (a = zzz, b = await 1) => void;
class K {
    constructor(a = zzz);
    constructor(a: any) {}
    m(a = new Nope()): void;
    m(a: any) {}
}
function* g1(a = yield): Generator; function* g1(a: any): Generator { }
