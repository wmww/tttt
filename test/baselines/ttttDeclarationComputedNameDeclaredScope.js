//// [test/cases/ttttDeclarationComputedNameDeclaredScope.ts] ////

//// [ambient.ts]
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
//// [local.ts]
const sym: unique symbol = Symbol();
export class L { [sym] = 1; }
//// [other.ts]
export const other: unique symbol = Symbol();
export namespace Q { export const q: unique symbol = Symbol(); }
//// [alias.ts]
import { other, Q } from "./other";
export class A { [other] = 1; }
export class B { [Q.q] = 1; }
//// [main.ts]
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


//// [ambient.js]
"use strict";
var G;
(function (G) {
    var _a, _b;
    const g = Symbol();
    class GC {
        constructor() {
            this[_a] = 1;
        }
    }
    _a = g;
    G.GC = GC;
    G.e = Symbol();
    class GE {
        constructor() {
            this[_b] = 1;
        }
    }
    _b = G.e;
    G.GE = GE;
})(G || (G = {}));
//// [local.js]
"use strict";
var _a;
Object.defineProperty(exports, "__esModule", { value: true });
exports.L = void 0;
const sym = Symbol();
class L {
    constructor() {
        this[_a] = 1;
    }
}
exports.L = L;
_a = sym;
//// [other.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Q = exports.other = void 0;
exports.other = Symbol();
var Q;
(function (Q) {
    Q.q = Symbol();
})(Q || (exports.Q = Q = {}));
//// [alias.js]
"use strict";
var _a, _b;
Object.defineProperty(exports, "__esModule", { value: true });
exports.B = exports.A = void 0;
const other_1 = require("./other");
class A {
    constructor() {
        this[_a] = 1;
    }
}
exports.A = A;
_a = other_1.other;
class B {
    constructor() {
        this[_b] = 1;
    }
}
exports.B = B;
_b = other_1.Q.q;
//// [main.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.f1 = f1;
exports.f2 = f2;
exports.f3 = f3;
exports.f4 = f4;
exports.f5 = f5;
exports.f6 = f6;
exports.f7 = f7;
exports.g1 = g1;
exports.g2 = g2;
exports.g3 = g3;
exports.g4 = g4;
/// <reference path="ambient.ts" />
const E = require("ev");
const local_1 = require("./local");
const alias_1 = require("./alias");
const N = require("ns");
const make = require("tl");
const tl2_1 = require("tl2");
const NX = require("nx");
function f1() { return Object.assign({}, new E()); }
function f2() { return Object.assign({}, new local_1.L()); }
function f3() { return Object.assign({}, new alias_1.A()); }
function f4() { return Object.assign({}, new N.C()); }
function f5(x) { return Object.assign({}, new x.Implicit()); }
function f6() { return Object.assign({}, new NX.C()); }
function f7() { return Object.assign({}, new alias_1.B()); }
function g1() { return Object.assign({}, new G.GC()); }
function g2() { return Object.assign({}, new G.GE()); }
function g3() { return Object.assign({}, make()); }
function g4() { return Object.assign({}, (0, tl2_1.lit)()); }


//// [ambient.d.ts]
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
        class C {
            [hidden]: number;
        }
    }
    export = N;
}
declare module "nx" {
    namespace N {
        const shown: unique symbol;
        class C {
            [shown]: number;
        }
    }
    export = N;
}
declare namespace G {
    const g: unique symbol;
    export class GC {
        [g]: number;
    }
    export const e: unique symbol;
    export class GE {
        [e]: number;
    }
    export {};
}
declare module "tl" {
    const t: unique symbol;
    interface I {
        [t]: number;
    }
    type Lit = {
        [t]: string;
    };
    function make(): I;
    function lit(): Lit;
    export = make;
}
declare module "tl2" {
    const t2: unique symbol;
    type Lit = {
        [t2]: string;
    };
    function lit(): Lit;
    export { lit };
}
//// [local.d.ts]
declare const sym: unique symbol;
export declare class L {
    [sym]: number;
}
export {};
//// [other.d.ts]
export declare const other: unique symbol;
export declare namespace Q {
    const q: unique symbol;
}
//// [alias.d.ts]
import { other, Q } from "./other";
export declare class A {
    [other]: number;
}
export declare class B {
    [Q.q]: number;
}
