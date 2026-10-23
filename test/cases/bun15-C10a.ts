// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1<T extends undefined>(r: Required<{ a?: T }>) { const n: never = r.a; }
const sentinel: number = "";
