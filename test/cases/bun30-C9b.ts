// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export class C { #a = 1; m(o: { #a: number }) { return o.#a; } }
const sentinel: number = "";
