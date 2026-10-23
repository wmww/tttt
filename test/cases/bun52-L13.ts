// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function count(o: { A: number }): number;
enum E { A = 1, B = count(E) }
const sentinel: number = "";
