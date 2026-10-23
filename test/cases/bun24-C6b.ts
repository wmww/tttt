// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { static #p() { return C.#p; } static t() { const z: 1 = C.#p; } }
const sentinel: number = "";
