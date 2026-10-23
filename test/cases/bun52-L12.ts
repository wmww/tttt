// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g(x: number): number;
declare class K { constructor(x: number); }
function f1({ a = 1, b = g(a) }) { const s: string = a; }
function f2({ a = 1, b = new K(a) }) {}
function f3([a = 1, b = g(a)]) {}
const f4 = ({ a = 1, b = g(a) }) => {};
class C { m({ a = 1, b = g(a) }) {} }
const sentinel: number = "";
