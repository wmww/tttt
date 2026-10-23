// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "noLib": true, "target": "es2022", "module": "esnext" } }
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
interface Promise<T> {}
}
async function f(): Promise<{ a: number }> { return { a: 1 }; }
import("./b", { with: {} });
const sentinel: number = "";
// @filename: b.ts
export default 1;
