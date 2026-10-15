//// [test/cases/ttttDeclarationPatternPaintedByExport.ts] ////

//// [package.json]
{ "name": "lib", "version": "1.0.0", "types": "index.d.ts" }
//// [index.d.ts]
export interface Bundle<T> { a: () => T; b: () => T; c: () => T }
export declare function make<T>(): Bundle<T>;
//// [a.ts]
import { make } from "lib";
const { a, b } = make<string>();
const { c } = make<number>();
export { a, b };


//// [a.js]
import { make } from "lib";
const { a, b } = make();
const { c } = make();
export { a, b };


//// [a.d.ts]
declare const { a, b }: import("lib").Bundle<string>;
export { a, b };
