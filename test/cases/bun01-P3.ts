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
}
import("./b");
async function f1() {} f1();
const sentinel: number = "";
// @filename: b.ts
export default 1;
