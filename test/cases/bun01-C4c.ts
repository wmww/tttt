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
interface Promise {}
type Omit<T> = T;
interface TemplateStringsArray<T> {}
async function f1() { return 1; }
function tag(x: any) {}
tag`a`;
function f5<T extends { a: number }>(o: T) { const { a, ...rest } = o; }
const sentinel: number = "";
