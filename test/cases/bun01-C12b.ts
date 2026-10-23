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
type NonNullable<T, U> = T;
function f3<T>(o: T) { if (o != null) { const n: number = o; } }
const sentinel: number = "";
