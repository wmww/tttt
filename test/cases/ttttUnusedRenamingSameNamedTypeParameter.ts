// @strict: true
// @target: es2017
// @module: esnext
// @importHelpers: true
// tsc binds a signature's type parameters and parameter names into one symbol table, where a type parameter X and
// a renaming `{ a: X }` merge: a reference to the type X in the signature counts as a use of the renaming (no TS2842).
// A rest element with a property name (TS2566) is a renaming too, and a bodiless one needs no `__rest` helper (TS2354)
export {};
type D1 = <T, X>({ a: X }: { a: X }) => void;
type D2 = <T, X>({ a: X }: { a: any }) => X;
type D3 = <X>({ a: X }: { a: any }, b: X) => void;
type D4 = <X>({ a: X }: { a: any }) => void;
type D5 = <X>({ a: X }: { a: any }) => typeof X;
type D6 = <X>({ a: X }: { a: any }) => X is string;
type D7 = <X>({ a: { b: X } }: { a: { b: X } }) => void;
type D8 = <X, Y extends X>({ a: X }: { a: any }, y: Y) => void;
type D9 = <X>({ a: X, b: Y }: { a: X; b: any }) => void;
interface I { m<X>({ a: X }: { a: X }): void; n<X>({ a: X }: { a: any }): void; }
abstract class C { abstract m<X>({ a: X }: { a: any }): X; }
declare function f<X>({ a: X }: { a: X[] }): void;
type R = ({ ...a: b }: any) => void;
declare function g({ ...a: b }: any): void;
function h({ ...a: b }: { x: number }): void;
function h(x: any) {}
function k({ ...rest }: { x: number }): void;
function k(x: any) {}
