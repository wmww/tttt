// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const obj = { a: 1, b: { c: 2 } };
const { a: _a, ...rest } = obj;
declare function f1<T>(cb: (x: T) => typeof rest): T;
function g1(x: number) { return f1(g1); }
declare function f2<T>(x: { a: T, cb: () => typeof rest }): T;
function g2() { return f2({ a: 1, cb: g2 }); }
function h({ a, ...more }: { a: number, z: string }) {
  function f3<T>(cb: (x: T) => typeof more): T { throw 0; }
  function g3(x: number) { return f3(g3); }
}
const sentinel: number = "";
