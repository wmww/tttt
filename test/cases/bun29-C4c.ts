// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace P1 { export enum E { N = NaN } }
namespace P2 { export enum E { N = NaN } }
declare let q1: P1.E, q2: P2.E;
q1 = q2;
const sentinel: number = "";
