//// [test/cases/ttttJsDeclarationEmitCommonJs.ts] ////

//// [lib.js]
/** @typedef {{ x: number }} Coord */
class Point {
    constructor() {
        /** The x coordinate */
        this.x = 1;
        this["y-z"] = "a";
    }
}
module.exports = { Point };
module.exports.Point = Point;
//// [index.js]
const { Point } = require("./lib");
/** @typedef {{ a: number }} Options */
/**
 * @param {Options} o
 */
function make(o) { return o.a; }
exports.version = "1.0";
exports.make = make;
exports.Sub = class Sub { self = new Sub(); };
exports.Maker = class Maker {
    constructor() {
        this.made = make({ a: 1 });
        this.version = exports.version;
    }
};
//// [esm.js]
/** @import { Coord } from "./lib" */
/**
 * @overload
 * @param {number} x
 * @returns {number}
 */
/**
 * @overload
 * @param {string} x
 * @returns {string}
 */
/**
 * @param {unknown} x
 */
export function id(x) { return x; }
export class Holder {
    constructor() {
        /** @type {Coord | undefined} */
        this.point = undefined;
    }
}
Holder.count = 0;


//// [lib.js]
"use strict";
/** @typedef {{ x: number }} Coord */
class Point {
    constructor() {
        /** The x coordinate */
        this.x = 1;
        this["y-z"] = "a";
    }
}
module.exports = { Point };
module.exports.Point = Point;
//// [index.js]
"use strict";
const { Point } = require("./lib");
/** @typedef {{ a: number }} Options */
/**
 * @param {Options} o
 */
function make(o) { return o.a; }
exports.version = "1.0";
exports.make = make;
exports.Sub = class Sub {
    self = new Sub();
};
exports.Maker = class Maker {
    constructor() {
        this.made = make({ a: 1 });
        this.version = exports.version;
    }
};
//// [esm.js]
"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.Holder = void 0;
exports.id = id;
/** @import { Coord } from "./lib" */
/**
 * @overload
 * @param {number} x
 * @returns {number}
 */
/**
 * @overload
 * @param {string} x
 * @returns {string}
 */
/**
 * @param {unknown} x
 */
function id(x) { return x; }
class Holder {
    constructor() {
        /** @type {Coord | undefined} */
        this.point = undefined;
    }
}
exports.Holder = Holder;
Holder.count = 0;


//// [lib.d.ts]
declare const _exports: {
    Point: typeof Point;
};
export = _exports;
declare namespace _exports {
    export { Point };
}
export type Coord = {
    x: number;
};
/** @typedef {{ x: number }} Coord */
declare class Point {
    /** The x coordinate */
    x: number;
    "y-z": string;
    constructor();
}
//// [index.d.ts]
export declare var version: "1.0";
export { make };
declare namespace _ns {
    export class Sub {
        self: Sub;
    }
}
import _Sub = _ns.Sub;
export { _Sub as Sub };
export declare class Maker {
    made: number;
    version: "1.0";
    constructor();
}
export type Options = {
    a: number;
};
/** @typedef {{ a: number }} Options */
/**
 * @param {Options} o
 */
declare function make(o: Options): number;
//// [esm.d.ts]
import type { Coord } from "./lib";
export declare function id(x: number): number;
export declare function id(x: string): string;
export declare class Holder {
    /** @type {Coord | undefined} */
    point: Coord | undefined;
    constructor();
}
export declare namespace Holder {
    var count: number;
}
