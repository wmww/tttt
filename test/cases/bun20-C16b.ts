// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum SE { A = "a", B = "b" }
const z1: SE = [1];
const z2: boolean = { valueOf: 1 };
const sentinel: number = "";
