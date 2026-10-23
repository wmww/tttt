// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function k<T extends unknown[]>(a: Uppercase<string & [...T]>) { const x1: number = a; }
const sentinel: number = "";
