// @strict: true
// @noLib: true
// @target: es2022
// @module: esnext
// @noTypesAndSymbols: true
// The global types the checker asks for with tsc's reportErrors are TS2318 when the lib lacks them: Promise (an async
// function), TemplateStringsArray (a tagged template), Omit (a generic object rest), ImportMeta, ImportAttributes and
// ImportCallOptions, Record (`"a" in o` of an object), Extract (for-in over a generic); a global `interface Omit` is no
// type alias (bun's 01-C5a, 01-C5b, 01-C12a)
// @filename: b.ts
export default 1;
// @filename: a.ts
export {};
declare global {
interface Array<T> {}
interface Object {}
interface Function {}
interface String {}
interface Number {}
interface Boolean {}
interface RegExp {}
interface IArguments {}
interface CallableFunction {}
interface NewableFunction {}
}
async function f1() { return 1; }
function tag(x: any) {}
tag`a`;
function f5<T extends { a: number }>(o: T) { const { a, ...rest } = o; }
import.meta;
import x from "./b" with { type: "json" };
function f6(o: object) { if ("a" in o) { o; } }
function f7<T>(o: T) { for (const k in o) { k; } }
interface Omit<T, K> {}
import("./b", { with: {} });
