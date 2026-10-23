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
async function f1() { return 1; }
function tag(x: any) {}
tag`a`;
function f5<T extends { a: number }>(o: T) { const { a, ...rest } = o; }
import.meta;
import x from "./b" with { type: "json" };
function f6(o: object) { if ("a" in o) { o; } }
function f7<T>(o: T) { for (const k in o) { k; } }
const sentinel: number = "";
// @filename: b.ts
export default 1;
