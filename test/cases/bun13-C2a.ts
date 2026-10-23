// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum E2 { A = 1, H = E2.Z }
const sentinel: number = "";
