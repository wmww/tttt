// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { #p = new Map<string, 1>(); f(c: C, k: string) { c.#p[k]; } }
const sentinel: number = "";
