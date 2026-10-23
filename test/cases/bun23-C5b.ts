// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace M1 { export class D { a = 1; static n() { return { self: D.n, d: new D() }; } } }
namespace M2 { export class D { a = ""; static n() { return { self: D.n, d: new D() }; } } }
let q1 = M1.D.n; q1 = M2.D.n;
const sentinel: number = "";
