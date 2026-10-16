//// [test/cases/ttttDeclarationImportOfPropertyNotType.ts] ////

//// [m.d.ts]
declare global {
    interface Foo { Foo: FooCtor; hi(): void }
    interface FooCtor { new(): Foo }
    var foo: Foo;
}
export = globalThis.foo;
//// [a.ts]
import { Foo } from './m';
export class L {
    public c: Foo;
    constructor() { this.c = new Foo(); }
}
export const f = (c: Foo) => c;
//// [b.ts]
import { Foo } from './m';
import { f } from './a';
export const g = f;
export const h = (x: Foo) => [f(x), new Foo()];


//// [a.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.f = exports.L = void 0;
const m_1 = require("./m");
class L {
    c;
    constructor() { this.c = new m_1.Foo(); }
}
exports.L = L;
const f = (c) => c;
exports.f = f;
//// [b.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.h = exports.g = void 0;
const m_1 = require("./m");
const a_1 = require("./a");
exports.g = a_1.f;
const h = (x) => [(0, a_1.f)(x), new m_1.Foo()];
exports.h = h;


//// [a.d.ts]
export declare class L {
    c: Foo;
    constructor();
}
export declare const f: (c: Foo) => Foo;
//// [b.d.ts]
import { f } from './a';
export declare const g: typeof f;
export declare const h: (x: Foo) => Foo[];
