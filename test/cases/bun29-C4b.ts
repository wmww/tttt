// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace G1 { export enum E { N = -0, M = 1 } }
namespace G2 { export enum E { N = 0, M = 1 } }
declare let g1: G1.E, g2: G2.E;
g1 = g2;
const sentinel: number = "";
