// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022", "module": "esnext" } }
// @filename: a.ts
const i1 = import("./b");
const sentinel: number = "";
export {};
// @filename: b.ts
export const b = 1;
// @filename: lib.d.ts
interface Array<T> { length: number; [n: number]: T } interface Boolean { } interface CallableFunction { } interface Function { } interface IArguments { }
interface NewableFunction { } interface Number { } interface Object { } interface RegExp { } interface String { }
