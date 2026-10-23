// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const X: (new () => { a: number }) & { [k: string]: number };
class S extends X {}
const sentinel: number = "";
