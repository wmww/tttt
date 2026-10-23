// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
enum En { A, B }
const e: En = { toFixed: 1 };
const sentinel: number = "";
