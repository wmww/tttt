// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace N1 { export enum Z { A = -0 } }
namespace N2 { export enum Z { A = 0 } }
let n1: N1.Z = N2.Z.A;
const sentinel: number = "";
