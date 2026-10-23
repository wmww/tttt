// @declaration: true
// @strict: true
// @target: es2015
// @module: commonjs
// A late bound member name that doesn't resolve where the inferred type is printed is tracked as it resolves where
// it is written (tsc's trackComputedName fallback): a class an ambient module's export = names is accessible through
// the module (openclaw's EventEmitter, no TS4060); an export (tsc's local symbol of it), an implicit export of an ambient
// module and an import can't be named, from the module they're written in, an ambient one too
// @filename: ambient.ts
declare module "ev" {
    class EventEmitter {
        static readonly capture: unique symbol;
        [EventEmitter.capture]?: number;
    }
    export = EventEmitter;
}
declare module "imp" {
    const s1: unique symbol;
    class Implicit {
        [s1]: number;
    }
}
declare module "ns" {
    namespace N {
        const hidden: unique symbol;
        class C { [hidden]: number; }
    }
    export = N;
}
declare module "nx" {
    namespace N {
        export const shown: unique symbol;
        export class C { [shown]: number; }
    }
    export = N;
}
namespace G {
    const g: unique symbol = Symbol();
    export class GC { [g] = 1; }
    export const e: unique symbol = Symbol();
    export class GE { [e] = 1; }
}
declare module "tl" {
    const t: unique symbol;
    interface I { [t]: number }
    type Lit = { [t]: string };
    function make(): I;
    function lit(): Lit;
    export = make;
}
declare module "tl2" {
    const t2: unique symbol;
    type Lit = { [t2]: string };
    function lit(): Lit;
    export { lit };
}
// @filename: local.ts
const sym: unique symbol = Symbol();
export class L { [sym] = 1; }
// @filename: other.ts
export const other: unique symbol = Symbol();
export namespace Q { export const q: unique symbol = Symbol(); }
// @filename: alias.ts
import { other, Q } from "./other";
export class A { [other] = 1; }
export class B { [Q.q] = 1; }
// @filename: main.ts
/// <reference path="ambient.ts" />
import E = require("ev");
import { L } from "./local";
import { A, B } from "./alias";
import N = require("ns");
import make = require("tl");
import { lit } from "tl2";
import NX = require("nx");
export function f1() { return { ...new E() }; }
export function f2() { return { ...new L() }; }
export function f3() { return { ...new A() }; }
export function f4() { return { ...new N.C() }; }
export function f5(x: typeof import("imp")) { return { ...new x.Implicit() }; }
export function f6() { return { ...new NX.C() }; }
export function f7() { return { ...new B() }; }
export function g1() { return { ...new G.GC() }; }
export function g2() { return { ...new G.GE() }; }
export function g3() { return { ...make() }; }
export function g4() { return { ...lit() }; }
