// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f1<T>(cb: () => { a: (x: number) => void; v?: unknown }, t: T): T;
declare function f3<T>(cb: () => { a: (x: number) => void; v?: T }, t: T): T;
function g1() { return f1(() => ({ a: x => {}, v: g1() }), 1); }
function g3() { return f3(() => ({ a: x => {}, v: g3() }), 1); }
function g7() { return f1(function () { return { a: x => {}, v: g7() }; }, 1); }
const sentinel: number = "";
