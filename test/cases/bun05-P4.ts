// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022" } }
// @filename: a.ts
class K { static readonly #i = Symbol(); static t() { const n: number = K.#i; } }
const sentinel: number = "";
