// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022", "module": "esnext" } }
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
interface Omit<T, K> {}
function f2<T extends { a: number }>(o: T) { const { a, ...rest } = o; const n: number = rest; }
const sentinel: number = "";
