// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022" } }
// @filename: a.ts
interface Promise<T> { then<R>(ok: (value: T) => R): Promise<R>; }
async function f(): Promise<(x: number) => void> { return x => {}; }
const sentinel: number = "";
// @filename: lib.d.ts
interface Array<T> { length: number; [n: number]: T } interface Boolean { } interface CallableFunction { } interface Function { } interface IArguments { }
interface NewableFunction { } interface Number { } interface Object { } interface RegExp { } interface String { }
