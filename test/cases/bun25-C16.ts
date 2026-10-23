// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace A { export class K { a = 1 } } namespace B { export type K = [1] }
declare let v: A.K | B.K; const n: number = v;
const sentinel: number = "";
