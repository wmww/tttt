// @target: es2015
// @module: commonjs
// @filename: ambient.d.ts
// Ambient modules, `export =`, `import x = require()`, import types, module augmentation, `export as namespace`.
// tsc: errors only where marked
declare module "m" {
    export interface Opts { a: number }
    export function run(o: Opts): string;
}
declare module "m" {
    export const extra: boolean;
}
declare module "eq" {
    function f(x: number): string;
    namespace f { export interface Inner { i: number } export const v: number; }
    export = f;
}
declare module "*.css" {
    const classes: { [k: string]: string };
    export default classes;
}
declare module "short";

// @filename: cls.ts
class C { c = 1; static s = "s" }
export = C;

// @filename: a.ts
export interface A { a: string }
export class K<T> { k!: T }
export const value = 1;

// @filename: aug.ts
import { A } from "./a";
declare module "./a" {
    interface A { added: number }
    export function late(): void;
}
declare global {
    interface Array<T> { mine(): T }
}
export const a: A = { a: "", added: 1 };

// @filename: main.ts
/// <reference path="ambient.d.ts" />
import { run, Opts, extra } from "m";
import f = require("eq");
import C = require("./cls");
import css from "x.css";
import anything from "short";
import { late } from "./a";
import "./aug";
const o: Opts = { a: 1 };
const s: string = run(o);
const b: boolean = extra;
const r: string = f(1);
const n: number = f.v;
let inner: f.Inner = { i: 1 };
const c = new C();
const cn: number = c.c;
const cs: string = C.s;
const k: string = css.foo;
anything.whatever;
late();
[1].mine();
let t1: import("./a").A = { a: "", added: 2 };
let t2: import("./a").K<number>;
let t3: typeof import("./a");
const v1: number = t3.value;
const bad1: string = t3.value; // TS2322
let t4: import("./a").Missing; // TS2694
import { nope } from "m"; // TS2305
import * as none from "nowhere"; // TS2307
function g() { return import("./a"); }
