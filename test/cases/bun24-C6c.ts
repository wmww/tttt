// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class G { static #q() { return G.#q; } static t() { const z: 1 = { c: G.#q }; } }
const sentinel: number = "";
