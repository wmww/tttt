// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class E { "#y" = 1; m(a: E) { delete a.#y; } }
const sentinel: number = "";
