// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true } }
// @filename: a.ts
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
interface Symbol {}
interface SymbolConstructor { readonly iterator: unique symbol; }
declare var Symbol: SymbolConstructor;
interface Iterator<T, TReturn = any, TNext = any> { next(): { done: boolean; value: T }; }
interface Iterable<T, TReturn = any, TNext = any> { [Symbol.iterator](): Iterator<T, TReturn, TNext>; }
interface ReadonlyArray<T> {}
declare const arr: number[]; declare const ro: readonly number[]; declare const tup: [number, number];
const [e1] = arr;
const [e2, ...r2] = ro;
const [e3] = tup;
const [] = arr;
let e4: number; [e4] = arr;
function f5([a, b]: number[], [c]: [number]) {}
const { p: [q] } = { p: arr };
const [d1 = 1] = arr;
const sentinel: number = "";
