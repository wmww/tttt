// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E { N = -0, M = 1 }
let z: 0 = E.N;
const sentinel: number = "";
