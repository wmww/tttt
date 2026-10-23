// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E { A = -0, B = 1 }
const z: 0 = E.A;
const sentinel: number = "";
