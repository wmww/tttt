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
function f(t: [number], w: number[]) { for (const q of t) {} for (const q of w) {} }
const sentinel: number = "";
